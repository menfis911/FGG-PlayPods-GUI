#include "bt.h"
#include "gui.h"
#include "hci.h"
#include "log.h"
#include "sdp.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

/* HCI opcodes: OGF << 10 | OCF. */
#define OP_INQUIRY              0x0401
#define OP_INQUIRY_CANCEL       0x0402
#define OP_CREATE_CONNECTION    0x0405
#define OP_DISCONNECT           0x0406
#define OP_ACCEPT_CONNECTION    0x0409
#define OP_LINK_KEY_REPLY       0x040B
#define OP_LINK_KEY_NEG_REPLY   0x040C
#define OP_PIN_CODE_REPLY       0x040D
#define OP_AUTH_REQUESTED       0x0411
#define OP_SET_ENCRYPTION       0x0413
#define OP_IO_CAP_REPLY         0x042B
#define OP_USER_CONFIRM_REPLY   0x042C
#define OP_SET_EVENT_MASK       0x0C01
#define OP_WRITE_LOCAL_NAME     0x0C13
#define OP_WRITE_SCAN_ENABLE    0x0C1A
#define OP_WRITE_CLASS_OF_DEV   0x0C24
#define OP_WRITE_INQUIRY_MODE   0x0C45
#define OP_WRITE_SSP_MODE       0x0C56
#define OP_READ_BUFFER_SIZE     0x1005

#define CID_SIGNALING 0x0001
#define PSM_SDP       0x0001
#define SDP_CID       0x0050        /* our end of the headset's SDP channel */

#define MAX_CHANNELS  4

/* ACL flow control. The controller reports sent packets with Number Of
 * Completed Packets events, but on the shared controller a few of those go
 * to the system's driver. A packet not reported within INFLIGHT_MS is taken
 * as sent; the radio sends one in far less time. */
#define INFLIGHT_MAX  64
#define INFLIGHT_MS   60

typedef struct {
    int valid;
    unsigned char addr[6];
    unsigned char key[16];
    unsigned char type;
} link_key;

/* ---- state, all written by the dispatcher ------------------------------ */

static const char *g_key_path;
static link_key g_key;

static unsigned g_cc_op;
static unsigned char g_cc[HCI_PKT_MAX];
static int g_cc_len;

static int g_inq_done, g_found;
static bt_device g_devices[BT_MAX_DEVICES];
static int g_device_count;
static unsigned char g_target[6];
static int g_target_selected;
static unsigned char g_target_psrm;
static unsigned g_target_clock;

static int g_conn_done, g_conn_status;
static unsigned g_handle;
static int g_auth_done, g_auth_status;
static int g_enc_status, g_enc_on;
static int g_disconnected;

static unsigned char g_sig_id = 1;
static l2cap_chan *g_chans[MAX_CHANNELS];
static bt_frame_fn g_frame_fns[MAX_CHANNELS];
static int g_nchans;
static l2cap_chan g_sdp = { "sdp", SDP_CID, 0, 0, 0, 0, 0, 0, 672 };

static unsigned char g_l2buf[4096];
static int g_l2len, g_l2need;

static unsigned g_acl_mtu;
static int g_credits, g_credits_max;
static long g_inflight[INFLIGHT_MAX];
static int g_if_head, g_if_count;
static long g_assumed;
static long g_sent, g_reported;     /* ACL packets, and their reports */
static long g_system_replies;

static void (*g_tick)(void);
static long g_last_tick;

/* ---- helpers ----------------------------------------------------------- */

static const char *addr_str(const unsigned char *a)
{
    static char buf[4][18];
    static int which;
    char *out = buf[which++ & 3];

    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             a[5], a[4], a[3], a[2], a[1], a[0]);
    return out;
}

static void key_load(void)
{
    FILE *f = fopen(g_key_path, "rb");
    unsigned char rec[23];

    memset(&g_key, 0, sizeof g_key);
    if (!f) return;
    if (fread(rec, 1, sizeof rec, f) == sizeof rec) {
        memcpy(g_key.addr, rec, 6);
        memcpy(g_key.key, rec + 6, 16);
        g_key.type = rec[22];
        g_key.valid = 1;
    }
    fclose(f);
}

static void key_save(void)
{
    FILE *f = fopen(g_key_path, "wb");
    unsigned char rec[23];

    if (!f) {
        log_line("link key: cannot write %s errno=%d", g_key_path, errno);
        return;
    }
    memcpy(rec, g_key.addr, 6);
    memcpy(rec + 6, g_key.key, 16);
    rec[22] = g_key.type;
    fwrite(rec, 1, sizeof rec, f);
    fclose(f);
}

static void key_forget(void)
{
    memset(&g_key, 0, sizeof g_key);
    remove(g_key_path);
}

static l2cap_chan *chan_by_scid(unsigned cid)
{
    int i;

    if (cid == SDP_CID) return &g_sdp;
    for (i = 0; i < g_nchans; i++)
        if (g_chans[i]->scid == cid) return g_chans[i];
    return NULL;
}

/* The controller is shared with the system, which runs its own links on it
 * (the DualSense reconnects through it). Only events about the headset are
 * acted on; everything else is left to the system's driver. */
static int is_ours_addr(const unsigned char *a)
{
    return memcmp(a, g_target, 6) == 0;
}

static int is_ours_handle(const unsigned char *h)
{
    return g_conn_done && g_conn_status == 0 && (le16(h) & 0x0FFF) == g_handle;
}

/* Opcodes this payload sends. A reply to any other belongs to the system's
 * driver, which normally leaves this controller alone; it is logged, since
 * such a reply never reaches the command's owner. */
static int is_our_opcode(unsigned op)
{
    static const unsigned ours[] = {
        OP_INQUIRY, OP_INQUIRY_CANCEL, OP_CREATE_CONNECTION, OP_DISCONNECT,
        OP_ACCEPT_CONNECTION, OP_LINK_KEY_REPLY, OP_LINK_KEY_NEG_REPLY,
        OP_PIN_CODE_REPLY, OP_AUTH_REQUESTED, OP_SET_ENCRYPTION,
        OP_IO_CAP_REPLY, OP_USER_CONFIRM_REPLY, OP_SET_EVENT_MASK,
        OP_WRITE_LOCAL_NAME, OP_WRITE_SCAN_ENABLE, OP_WRITE_CLASS_OF_DEV,
        OP_WRITE_INQUIRY_MODE, OP_WRITE_SSP_MODE, OP_READ_BUFFER_SIZE,
    };
    size_t i;

    if (op == 0) return 1;              /* no-op: command credits only */
    for (i = 0; i < sizeof ours / sizeof ours[0]; i++)
        if (ours[i] == op) return 1;
    return 0;
}

static void system_reply(unsigned op, unsigned status)
{
    if (g_system_replies++ < 50)
        log_line("reply to the system's command %#06x (status %#04x)", op, status);
}

/* ---- ACL flow control -------------------------------------------------- */

static void inflight_push(void)
{
    if (g_if_count == INFLIGHT_MAX) {
        g_if_head = (g_if_head + 1) % INFLIGHT_MAX;
        g_if_count--;
    }
    g_inflight[(g_if_head + g_if_count) % INFLIGHT_MAX] = now_ms();
    g_if_count++;
}

static void inflight_done(int n)
{
    while (n-- > 0 && g_if_count > 0) {
        g_if_head = (g_if_head + 1) % INFLIGHT_MAX;
        g_if_count--;
    }
}

/* Gives back credits for packets whose completion report never reached us. */
static void inflight_expire(void)
{
    long now = now_ms();

    while (g_if_count > 0 && now - g_inflight[g_if_head] > INFLIGHT_MS) {
        g_if_head = (g_if_head + 1) % INFLIGHT_MAX;
        g_if_count--;
        g_assumed++;
        if (g_credits < g_credits_max) g_credits++;
    }
}

int bt_can_send(void)
{
    inflight_expire();
    return g_credits > 0;
}

int  bt_credits(void)              { return g_credits; }
long bt_reports_missing(void)      { return g_sent - g_reported; }
long bt_completions_assumed(void)  { return g_assumed; }
int  bt_link_lost(void)            { return g_disconnected; }
void bt_set_tick(void (*fn)(void)) { g_tick = fn; }

int bt_max_frame(void)
{
    return g_acl_mtu ? (int)g_acl_mtu - 4 : HCI_PKT_MAX - 8;
}

/* ---- L2CAP output ------------------------------------------------------ */

int bt_send(unsigned dcid, const unsigned char *data, int len)
{
    unsigned char pkt[HCI_PKT_MAX];

    if (8 + len > (int)sizeof pkt || (g_acl_mtu && 4 + len > (int)g_acl_mtu)) {
        log_line("l2cap: %d-byte frame too large to send unfragmented", len);
        return 0;
    }
    put16(pkt, (g_handle & 0x0FFF) | 0x2000);   /* first, auto-flushable */
    put16(pkt + 2, (unsigned)(4 + len));
    put16(pkt + 4, (unsigned)len);
    put16(pkt + 6, dcid);
    memcpy(pkt + 8, data, (size_t)len);
    if (!hci_acl_send(pkt, 8 + len)) return 0;
    g_credits--;
    g_sent++;
    inflight_push();
    return 1;
}

static int sig_send(unsigned char code, unsigned char id,
                    const unsigned char *data, int len)
{
    unsigned char p[128];

    p[0] = code;
    p[1] = id;
    put16(p + 2, (unsigned)len);
    if (len > 0) memcpy(p + 4, data, (size_t)len);
    return bt_send(CID_SIGNALING, p, 4 + len);
}

/* ---- inbound: HCI events ----------------------------------------------- */

static void on_inquiry_record(const unsigned char *r, const unsigned char *eir,
                              int eirlen)
{
    unsigned cod = (unsigned)r[8] | (unsigned)r[9] << 8 | (unsigned)r[10] << 16;
    char name[64] = "";
    int i = 0;

    while (eir && i + 1 < eirlen) {
        int flen = eir[i];
        if (flen == 0 || i + 1 + flen > eirlen) break;
        if (eir[i + 1] == 0x08 || eir[i + 1] == 0x09) {
            int n = flen - 1;
            if (n > (int)sizeof name - 1) n = (int)sizeof name - 1;
            memcpy(name, eir + i + 2, (size_t)n);
            name[n] = '\0';
        }
        i += 1 + flen;
    }

    /* Major device class 4: audio/video. Keep every result for the GUI. */
    if (((cod >> 8) & 0x1F) == 4) {
        int idx;
        for (idx = 0; idx < g_device_count; ++idx)
            if (memcmp(g_devices[idx].addr, r, 6) == 0)
                break;

        if (idx == g_device_count && g_device_count < BT_MAX_DEVICES) {
            bt_device *d = &g_devices[g_device_count++];
            memset(d, 0, sizeof(*d));
            d->valid = 1;
            memcpy(d->addr, r, 6);
            d->cod = cod;
            d->rssi = -127;
            snprintf(d->name, sizeof(d->name), "%s", name[0] ? name : "(unknown)");
            log_line("found[%d] %s '%s' (class %06x)", idx, addr_str(r), d->name, cod);
        }

        if (idx < g_device_count) {
            bt_device *d = &g_devices[idx];
            d->cod = cod;
            if (name[0]) snprintf(d->name, sizeof(d->name), "%s", name);
        }
        g_found = g_device_count > 0;
    }
}

static void on_event(const unsigned char *ev, int len)
{
    unsigned char p[32];

    switch (ev[0]) {
    case 0x01:  /* Inquiry Complete */
        g_inq_done = 1;
        break;

    case 0x22:  /* Inquiry Result with RSSI */
    {
        int i;
        for (i = 0; i < ev[2]; i++) on_inquiry_record(ev + 3 + i * 14, NULL, 0);
        break;
    }

    case 0x2F:  /* Extended Inquiry Result */
        on_inquiry_record(ev + 3, ev + 17, len - 17);
        break;

    case 0x04:  /* Connection Request */
        if (!is_ours_addr(ev + 2) || ev[11] != 1) break;   /* the system's */
        log_line("headset connecting to us by itself");
        memcpy(p, ev + 2, 6);
        p[6] = 0x01;    /* remain peripheral: no role switch to fail */
        hci_cmd(OP_ACCEPT_CONNECTION, p, 7);
        break;

    case 0x03:  /* Connection Complete */
        if (!is_ours_addr(ev + 5)) break;
        if (ev[2] == 0x0B && g_conn_done) break;    /* already have it */
        g_conn_status = ev[2];
        g_handle = le16(ev + 3) & 0x0FFF;
        g_conn_done = 1;
        log_line("connection complete: status %#04x handle %#05x", ev[2], g_handle);
        break;

    case 0x05:  /* Disconnection Complete */
        if (!is_ours_handle(ev + 3)) break;
        log_line("disconnected: status %#04x reason %#04x", ev[2], ev[5]);
        g_disconnected = 1;
        break;

    case 0x06:  /* Authentication Complete */
        if (!is_ours_handle(ev + 3)) break;
        g_auth_status = ev[2];
        g_auth_done = 1;
        log_line("authentication complete: status %#04x", ev[2]);
        break;

    case 0x08:  /* Encryption Change */
        if (!is_ours_handle(ev + 3)) break;
        g_enc_status = ev[2];
        if (ev[2] == 0) g_enc_on = ev[5];
        log_line("encryption change: status %#04x enabled %u", ev[2], ev[5]);
        break;

    case 0x0E:  /* Command Complete */
        if (!is_our_opcode(le16(ev + 3))) system_reply(le16(ev + 3), ev[5]);
        g_cc_op = le16(ev + 3);
        g_cc_len = len < (int)sizeof g_cc ? len : (int)sizeof g_cc;
        memcpy(g_cc, ev, (size_t)g_cc_len);
        break;

    case 0x0F:  /* Command Status */
        if (!is_our_opcode(le16(ev + 4))) system_reply(le16(ev + 4), ev[2]);
        if (ev[2] != 0) {
            unsigned op = le16(ev + 4);
            if (op == OP_CREATE_CONNECTION) {
                g_conn_status = ev[2];
                g_conn_done = 1;
            } else if (op == OP_AUTH_REQUESTED) {
                g_auth_status = ev[2];
                g_auth_done = 1;
            }
        }
        break;

    case 0x13:  /* Number Of Completed Packets */
    {
        /* (handle, count) pairs; only the headset link's are ours. */
        int i, n = ev[2];
        for (i = 0; i < n && 3 + i * 4 + 4 <= len; i++) {
            int done = (int)le16(ev + 3 + i * 4 + 2);
            if (!is_ours_handle(ev + 3 + i * 4)) continue;
            g_credits += done;
            g_reported += done;
            inflight_done(done);
        }
        if (g_credits_max && g_credits > g_credits_max) g_credits = g_credits_max;
        break;
    }

    case 0x16:  /* PIN Code Request: legacy pairing, try 0000 */
        if (!is_ours_addr(ev + 2)) break;
        log_line("PIN code requested, replying 0000");
        memset(p, 0, sizeof p);
        memcpy(p, ev + 2, 6);
        p[6] = 4;
        memcpy(p + 7, "0000", 4);
        hci_cmd(OP_PIN_CODE_REPLY, p, 23);
        break;

    case 0x17:  /* Link Key Request */
        if (!is_ours_addr(ev + 2)) break;
        if (g_key.valid && memcmp(g_key.addr, ev + 2, 6) == 0) {
            memcpy(p, ev + 2, 6);
            memcpy(p + 6, g_key.key, 16);
            hci_cmd(OP_LINK_KEY_REPLY, p, 22);
        } else {
            log_line("no stored key: pairing");
            hci_cmd(OP_LINK_KEY_NEG_REPLY, ev + 2, 6);
        }
        break;

    case 0x18:  /* Link Key Notification */
        if (!is_ours_addr(ev + 2)) break;
        memcpy(g_key.addr, ev + 2, 6);
        memcpy(g_key.key, ev + 8, 16);
        g_key.type = ev[24];
        g_key.valid = 1;
        key_save();
        log_line("paired with %s, key saved", addr_str(ev + 2));
        break;

    case 0x31:  /* IO Capability Request */
        if (!is_ours_addr(ev + 2)) break;
        memcpy(p, ev + 2, 6);
        p[6] = 0x03;    /* NoInputNoOutput */
        p[7] = 0x00;    /* no OOB data */
        p[8] = 0x04;    /* general bonding, MITM not required */
        hci_cmd(OP_IO_CAP_REPLY, p, 9);
        break;

    case 0x33:  /* User Confirmation Request */
        if (!is_ours_addr(ev + 2)) break;
        hci_cmd(OP_USER_CONFIRM_REPLY, ev + 2, 6);
        break;

    case 0xFF:  /* vendor event: the chip reporting an internal fault */
    {
        char hex[3 * 32 + 1];
        int i, n = 0;

        for (i = 0; i < len && i < 32; i++)
            n += snprintf(hex + n, sizeof hex - (size_t)n, "%02x ", ev[i]);
        log_line("chip vendor event (%d bytes): %s", len, hex);
        break;
    }

    default:
        break;
    }
}

/* ---- inbound: L2CAP ---------------------------------------------------- */

static void on_signaling(const unsigned char *d, int len)
{
    while (len >= 4) {
        unsigned char code = d[0], id = d[1];
        int clen = (int)le16(d + 2);
        const unsigned char *c = d + 4;
        unsigned char r[16];
        l2cap_chan *ch;
        int i;

        if (4 + clen > len) break;

        switch (code) {
        case 0x02:  /* Connection Request from the headset */
            if (le16(c) == PSM_SDP) {
                /* It looks up our service record: accept, configure. */
                g_sdp.dcid = le16(c + 2);
                g_sdp.conn_done = 1;
                g_sdp.closed = 0;
                g_sdp.cfg_rsp_ok = g_sdp.cfg_req_done = 0;
                put16(r, SDP_CID);
                put16(r + 2, g_sdp.dcid);
                put16(r + 4, 0);        /* success */
                put16(r + 6, 0);
                sig_send(0x03, id, r, 8);
                put16(r, g_sdp.dcid);
                put16(r + 2, 0);
                sig_send(0x04, g_sig_id++, r, 4);
                break;
            }
            /* Anything else (AVRCP, for one) is not offered. */
            put16(r, 0);
            put16(r + 2, le16(c + 2));
            put16(r + 4, 0x0002);       /* PSM not supported */
            put16(r + 6, 0);
            sig_send(0x03, id, r, 8);
            break;

        case 0x03:  /* Connection Response */
            ch = chan_by_scid(le16(c + 2));
            if (ch && le16(c + 4) != 1) {       /* 1: pending */
                ch->conn_result = (int)le16(c + 4);
                ch->dcid = le16(c);
                ch->conn_done = 1;
            }
            break;

        case 0x04:  /* Configuration Request */
            ch = chan_by_scid(le16(c));
            if (ch) {
                for (i = 4; i + 2 <= clen; i += 2 + c[i + 1]) {
                    if ((c[i] & 0x7F) == 0x01 && c[i + 1] == 2)
                        ch->remote_mtu = le16(c + i + 2);
                }
                put16(r, ch->dcid);
                put16(r + 2, 0);        /* flags */
                put16(r + 4, 0);        /* success */
                sig_send(0x05, id, r, 6);
                ch->cfg_req_done = 1;
            }
            break;

        case 0x05:  /* Configuration Response */
            ch = chan_by_scid(le16(c));
            if (!ch) {
                /* Answered with an unexpected id: it belongs to the channel
                 * still being configured, the latest one opened. */
                for (i = g_nchans - 1; i >= 0 && !ch; i--)
                    if (g_chans[i]->conn_done && !g_chans[i]->cfg_rsp_ok)
                        ch = g_chans[i];
                if (!ch && g_nchans) ch = g_chans[0];
            }
            if (ch && le16(c + 4) == 0) ch->cfg_rsp_ok = 1;
            break;

        case 0x06:  /* Disconnection Request */
            ch = chan_by_scid(le16(c));
            sig_send(0x07, id, c, 4);
            if (ch) ch->closed = 1;
            if (ch == &g_sdp) g_sdp.dcid = 0;   /* it may open another later */
            break;

        case 0x07:  /* Disconnection Response */
            ch = chan_by_scid(le16(c + 2));
            if (ch) ch->closed = 1;
            break;

        case 0x08:  /* Echo Request */
            sig_send(0x09, id, c, clen);
            break;

        case 0x0A:  /* Information Request */
        {
            unsigned type = le16(c);
            memset(r, 0, sizeof r);
            put16(r, type);
            if (type == 2) {                /* extended features: none */
                sig_send(0x0B, id, r, 8);
            } else if (type == 3) {         /* fixed channels: signaling */
                r[4] = 0x02;
                sig_send(0x0B, id, r, 12);
            } else {
                put16(r + 2, 1);            /* not supported */
                sig_send(0x0B, id, r, 4);
            }
            break;
        }

        default:
            break;
        }

        d += 4 + clen;
        len -= 4 + clen;
    }
}

static void on_l2cap_frame(unsigned cid, const unsigned char *d, int len)
{
    int i;

    if (cid == CID_SIGNALING) {
        on_signaling(d, len);
    } else if (cid == SDP_CID) {
        unsigned char rsp[512];
        int n = sdp_handle(d, len, rsp, (int)sizeof rsp);
        if (n > 0 && g_sdp.dcid) bt_send(g_sdp.dcid, rsp, n);
    } else {
        for (i = 0; i < g_nchans; i++)
            if (g_chans[i]->scid == cid && g_frame_fns[i]) g_frame_fns[i](cid, d, len);
    }
}

static void on_acl(const unsigned char *pkt, int len)
{
    unsigned hdr = le16(pkt);
    int pb = (int)((hdr >> 12) & 3);
    int dlen = (int)le16(pkt + 2);
    const unsigned char *d = pkt + 4;

    if (g_conn_done && (hdr & 0x0FFF) != g_handle) return;     /* not ours */
    if (4 + dlen > len) return;

    if (pb == 1) {                              /* continuation */
        if (g_l2need == 0) return;
        if (g_l2len + dlen > (int)sizeof g_l2buf) {
            g_l2need = 0;
            return;
        }
        memcpy(g_l2buf + g_l2len, d, (size_t)dlen);
        g_l2len += dlen;
    } else {                                    /* start of an L2CAP frame */
        if (dlen < 4 || dlen > (int)sizeof g_l2buf) return;
        memcpy(g_l2buf, d, (size_t)dlen);
        g_l2len = dlen;
        g_l2need = 4 + (int)le16(d);
    }

    if (g_l2need && g_l2len >= g_l2need) {
        on_l2cap_frame(le16(g_l2buf + 2), g_l2buf + 4, g_l2need - 4);
        g_l2need = 0;
    }
}

/* ---- pumping and waiting ----------------------------------------------- */

void bt_poll(int timeout_ms)
{
    unsigned char buf[HCI_PKT_MAX];
    int n;

    hci_pump(timeout_ms);
    while ((n = hci_next_event(buf, (int)sizeof buf)) > 0) on_event(buf, n);
    while ((n = hci_next_acl(buf, (int)sizeof buf)) > 0) on_acl(buf, n);

    gui_tick();

    if (g_tick && now_ms() - g_last_tick >= 1000) {
        g_last_tick = now_ms();
        g_tick();
    }
}

int bt_wait(volatile int *flag, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;

    while (!*flag) {
        long left = deadline - now_ms();
        if (left <= 0) return 0;
        bt_poll(left > 100 ? 100 : (int)left);
        if (g_disconnected && flag != &g_disconnected) return 0;
    }
    return 1;
}

/* Sends a command and waits for its Command Complete. On the shared
 * controller the reply can go to the system's driver, so the command is sent
 * again; the ones used here are safe to repeat. */
static int hci_sync(unsigned opcode, const void *params, int plen)
{
    int attempt;

    for (attempt = 1; attempt <= 3; attempt++) {
        long deadline = now_ms() + (attempt == 3 ? 3000 : 1000);

        g_cc_op = 0;
        if (!hci_cmd(opcode, params, plen)) return 0;
        while (g_cc_op != opcode && now_ms() < deadline) bt_poll(50);
        if (g_cc_op == opcode) return g_cc_len > 5 && g_cc[5] == 0;
    }
    log_line("command %#06x: no reply", opcode);
    return 0;
}

/* ---- controller -------------------------------------------------------- */

/* Prepares the controller. No reset: the system's driver has it set up, and
 * a reset would pull that state from under it. */
static int setup_controller(void)
{
    static const unsigned char mask[8] = { 0xFF, 0xFF, 0xFF, 0xFF,
                                           0xFF, 0xFF, 0xFF, 0x3F };
    static const unsigned char one[1] = { 1 };
    static const unsigned char two[1] = { 2 };
    /* Major class computer, minor desktop: an ordinary audio source. */
    static const unsigned char cod[3] = { 0x04, 0x01, 0x00 };
    unsigned char name[248];

    if (!hci_sync(OP_READ_BUFFER_SIZE, NULL, 0)) return 0;
    g_acl_mtu = le16(g_cc + 6);
    g_credits = g_credits_max = (int)le16(g_cc + 9);
    log_line("controller: %d ACL buffers of %u bytes", g_credits, g_acl_mtu);

    hci_sync(OP_SET_EVENT_MASK, mask, (int)sizeof mask);
    hci_sync(OP_WRITE_SSP_MODE, one, 1);
    hci_sync(OP_WRITE_INQUIRY_MODE, two, 1);
    hci_sync(OP_WRITE_CLASS_OF_DEV, cod, 3);

    memset(name, 0, sizeof name);
    memcpy(name, "FGG-PlayPods", 12);
    hci_sync(OP_WRITE_LOCAL_NAME, name, (int)sizeof name);

    /* Page scan on, inquiry scan off: a paired headset reconnects to its
     * source by itself, and must be able to reach us to do it. */
    hci_sync(OP_WRITE_SCAN_ENABLE, two, 1);
    return 1;
}

int bt_start(void)
{
    int attempt;

    /* Right after a previous session the controller can take a moment to
     * answer; closing and trying again a little later works. */
    for (attempt = 1; attempt <= 4; attempt++) {
        if (!hci_open()) return 0;
        if (setup_controller()) return 1;
        hci_close();
        log_line("controller not answering (attempt %d), retrying in 3 s", attempt);
        sleep(3);
    }
    return 0;
}

void bt_stop(void)
{
    unsigned char r[3];
    int attempt;

    if (g_conn_done && g_conn_status == 0 && !g_disconnected) {
        /* The completion can go to the system's driver; asking again is
         * harmless, and a link left behind blocks the next session. */
        for (attempt = 0; attempt < 3 && !g_disconnected; attempt++) {
            put16(r, g_handle);
            r[2] = 0x13;    /* remote user terminated connection */
            hci_cmd(OP_DISCONNECT, r, 3);
            bt_wait(&g_disconnected, 2000);
        }
    }
    hci_close();
}

/* ---- connection -------------------------------------------------------- */

static int find_headset(void)
{
    static const unsigned char inquiry[5] = { 0x33, 0x8B, 0x9E, 0x08, 0x00 };
    long deadline;

    if (g_key.valid) {
        memcpy(g_target, g_key.addr, 6);
        g_target_psrm = 0x01;
        g_target_clock = 0;
        log_line("headset paired before: %s", addr_str(g_target));
        return 1;
    }

    notify("FGG-PlayPods: searching - put the headset in pairing mode");
    log_line("searching for a headset in pairing mode");
    g_found = 0;
    g_inq_done = 0;
    if (!hci_cmd(OP_INQUIRY, inquiry, (int)sizeof inquiry)) return 0;

    deadline = now_ms() + 15000;
    while (!g_found && !g_inq_done && now_ms() < deadline) bt_poll(100);
    if (!g_inq_done) {
        hci_cmd(OP_INQUIRY_CANCEL, NULL, 0);
        bt_poll(200);
    }
    if (!g_found) {
        log_line("no headset in pairing mode found");
        return 0;
    }
    return 1;
}

int bt_device_count(void) { return g_device_count; }

const bt_device *bt_device_get(int index)
{
    if (index < 0 || index >= g_device_count) return NULL;
    return &g_devices[index];
}

int bt_scan_start(void)
{
    static const unsigned char inquiry[5] = { 0x33, 0x8B, 0x9E, 0x08, 0x00 };
    memset(g_devices, 0, sizeof g_devices);
    g_device_count = 0;
    g_found = 0;
    g_inq_done = 0;
    log_line("scan: started");
    return hci_cmd(OP_INQUIRY, inquiry, (int)sizeof inquiry);
}

int bt_scan_poll(void)
{
    bt_poll(50);
    return g_inq_done;
}

int bt_connect_device(int index, const char *key_path)
{
    const bt_device *d = bt_device_get(index);
    if (!d) return 0;
    g_key_path = key_path;
    key_load();
    memcpy(g_target, d->addr, 6);
    g_target_psrm = 0x01;
    g_target_clock = 0;
    g_target_selected = 1;
    log_line("selected device[%d]: %s '%s'", index, addr_str(d->addr), d->name);
    return bt_connect(key_path);
}

int bt_connect(const char *key_path)
{
    unsigned char p[16];
    long deadline;

    g_key_path = key_path;
    key_load();
    if (!g_target_selected) {
        if (!find_headset()) return 0;
    } else {
        log_line("using GUI-selected headset: %s", addr_str(g_target));
    }

    memcpy(p, g_target, 6);
    put16(p + 6, 0xCC18);           /* DM1..DH5 */
    p[8] = g_target_psrm;
    p[9] = 0;
    put16(p + 10, g_target_clock);
    p[12] = 1;                      /* allow role switch */

    notify("FGG-PlayPods: connecting to the headset");
    g_conn_done = 0;

    /* A paired headset that is switched on reconnects by itself; give it a
     * moment before paging it. */
    if (g_key.valid && bt_wait(&g_conn_done, 3000) && g_conn_status == 0) {
        log_line("headset connected by itself");
    } else {
        g_conn_done = 0;
        if (!hci_cmd(OP_CREATE_CONNECTION, p, 13)) return 0;
        if (!bt_wait(&g_conn_done, 20000)) {
            log_line("connection: no answer");
            return 0;
        }
        if (g_conn_status == 0x0B) {
            /* Already linked: the headset is completing its own
             * reconnection, or a link from an earlier session remains whose
             * handle we never learned. Other handles on this controller can
             * be the system's, so nothing is disconnected blindly. */
            g_conn_done = 0;
            if (!bt_wait(&g_conn_done, 10000)) {
                log_line("connection: the headset is still linked from an "
                         "earlier session");
                notify("FGG-PlayPods: turn the headset off and on, then try again");
                return 0;
            }
        }
    }
    if (g_conn_status != 0) {
        log_line("connection failed: status %#04x%s", g_conn_status,
                 g_conn_status == 0x04 ? " (headset off, out of range, or "
                                         "connected to another device)" : "");
        return 0;
    }

    put16(p, g_handle);
    g_auth_done = 0;
    if (!hci_cmd(OP_AUTH_REQUESTED, p, 2)) return 0;
    if (!bt_wait(&g_auth_done, 30000)) {
        log_line("authentication: no answer");
        return 0;
    }
    if (g_auth_status != 0) {
        log_line("authentication failed: status %#04x", g_auth_status);
        if (g_key.valid) {
            log_line("forgetting the stored key; pair the headset again");
            key_forget();
        }
        return 0;
    }

    /* A headset that reconnected by itself starts encryption by itself, and
     * asking at the same moment collides (status 0x2f). Give it the chance
     * first, and accept a failed request as long as encryption ends up on. */
    bt_wait(&g_enc_on, 2000);
    if (!g_enc_on) {
        put16(p, g_handle);
        p[2] = 1;
        if (!hci_cmd(OP_SET_ENCRYPTION, p, 3)) return 0;
    }
    deadline = now_ms() + 8000;
    while (!g_enc_on && !g_disconnected && now_ms() < deadline) bt_poll(100);
    if (!g_enc_on) {
        log_line("encryption failed: status %#04x", g_enc_status);
        return 0;
    }
    log_line("link encrypted");
    return 1;
}

/* ---- channels ---------------------------------------------------------- */

int bt_open_channel(l2cap_chan *ch, unsigned psm, bt_frame_fn on_frame)
{
    unsigned char r[8];
    long deadline, resend = 0;

    if (g_nchans < MAX_CHANNELS && !chan_by_scid(ch->scid)) {
        g_chans[g_nchans] = ch;
        g_frame_fns[g_nchans] = on_frame;
        g_nchans++;
    }

    put16(r, psm);
    put16(r + 2, ch->scid);
    ch->conn_done = 0;
    if (!sig_send(0x02, g_sig_id++, r, 4)) return 0;
    if (!bt_wait(&ch->conn_done, 10000)) {
        log_line("l2cap: %s connection timed out", ch->name);
        return 0;
    }
    if (ch->conn_result != 0) {
        log_line("l2cap: %s connection refused (%d)", ch->name, ch->conn_result);
        return 0;
    }

    /* Some of the headset's packets go to the system's driver: ask again
     * until our configuration is answered, and give the headset time to
     * repeat its own request. */
    ch->cfg_rsp_ok = 0;
    deadline = now_ms() + 10000;
    while (!(ch->cfg_rsp_ok && ch->cfg_req_done)) {
        if (now_ms() >= deadline || g_disconnected) {
            log_line("l2cap: %s configuration did not finish", ch->name);
            return 0;
        }
        if (!ch->cfg_rsp_ok && now_ms() >= resend) {
            put16(r, ch->dcid);
            put16(r + 2, 0);
            if (!sig_send(0x04, g_sig_id++, r, 4)) return 0;
            resend = now_ms() + 1500;
        }
        bt_poll(100);
    }
    return 1;
}

void bt_close_channel(l2cap_chan *ch)
{
    unsigned char r[4];

    if (!ch->dcid || ch->closed || g_disconnected) return;
    put16(r, ch->dcid);
    put16(r + 2, ch->scid);
    sig_send(0x06, g_sig_id++, r, 4);
    bt_wait(&ch->closed, 2000);
}
