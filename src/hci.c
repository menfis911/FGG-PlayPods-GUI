#include "hci.h"
#include "log.h"
#include "util.h"

#include <sys/ioctl.h>
#include <sys/types.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BT_NODE "/dev/ugen0.2"

/* The chip's USB layout: two HCI functions, each an interface pair (HCI and
 * SCO) and each a controller with its own address.
 *
 *   iface 0/1: events 0x82, ACL in 0x81, ACL out 0x01   (used here)
 *   iface 2/3: events 0x85, ACL in 0x84, ACL out 0x04   (the system's)
 *
 * Commands for function 0 go on the control endpoint. */
#define EP_EVENT   0x82
#define EP_ACL_IN  0x81
#define EP_ACL_OUT 0x01

/* The system's driver keeps one read pending on each IN endpoint, and each
 * packet goes to whichever read is first in line; many pending reads win
 * most of them. The kernel refuses transfers past a memory budget of roughly
 * 65, so event reads get buffers sized for the largest HCI event. */
#define EVENT_READS 1
#define ACL_READS   1
#define EVENT_BUF   260
#define XFER_BUF    HCI_PKT_MAX

#define IX_ACL_OUT  (EVENT_READS + ACL_READS)
#define N_XFERS     (IX_ACL_OUT + 1)

#define QUEUE_LEN 32

typedef struct {
    unsigned char data[QUEUE_LEN][HCI_PKT_MAX];
    int len[QUEUE_LEN];
    int head, count;
} pkt_queue;

static int g_fd = -1;
static struct usb_fs_endpoint g_eps[N_XFERS];
static void *g_bufptr[N_XFERS][1];
static uint32_t g_buflen[N_XFERS][1];
static unsigned char g_buf[N_XFERS][XFER_BUF];
static int g_pending[N_XFERS];
static int g_event_reads, g_acl_reads;     /* how many actually opened */

static pkt_queue g_events, g_acl;

static void queue_push(pkt_queue *q, const unsigned char *p, int len)
{
    int slot;

    if (len > HCI_PKT_MAX) len = HCI_PKT_MAX;
    if (q->count == QUEUE_LEN) {
        log_line("hci: queue full, dropping oldest packet");
        q->head = (q->head + 1) % QUEUE_LEN;
        q->count--;
    }
    slot = (q->head + q->count) % QUEUE_LEN;
    memcpy(q->data[slot], p, (size_t)len);
    q->len[slot] = len;
    q->count++;
}

static int queue_pop(pkt_queue *q, unsigned char *out, int max)
{
    int n;

    if (q->count == 0) return 0;
    n = q->len[q->head];
    if (n > max) n = max;
    memcpy(out, q->data[q->head], (size_t)n);
    q->head = (q->head + 1) % QUEUE_LEN;
    q->count--;
    return n;
}

int hci_next_event(unsigned char *out, int max) { return queue_pop(&g_events, out, max); }
int hci_next_acl(unsigned char *out, int max)   { return queue_pop(&g_acl, out, max); }

static int is_event_read(int ix) { return ix < EVENT_READS; }

static int ep_open(int index, int addr, int bufsize)
{
    struct usb_fs_open op;

    memset(&op, 0, sizeof op);
    op.max_bufsize = (uint32_t)bufsize;
    op.max_frames = 1;
    op.ep_index = (uint8_t)index;
    op.ep_no = (uint8_t)addr;
    if (ioctl(g_fd, USB_FS_OPEN, &op) != 0) {
        log_line("hci: USB_FS_OPEN %#x (transfer %d) errno=%d", addr, index, errno);
        return 0;
    }
    return 1;
}

static int ep_start(int index, int length)
{
    struct usb_fs_start start;

    g_buflen[index][0] = (uint32_t)length;
    g_eps[index].nFrames = 1;
    g_eps[index].aFrames = 0;
    g_eps[index].status = 0;

    memset(&start, 0, sizeof start);
    start.ep_index = (uint8_t)index;
    if (ioctl(g_fd, USB_FS_START, &start) != 0) {
        log_line("hci: USB_FS_START %d errno=%d", index, errno);
        return 0;
    }
    g_pending[index] = 1;
    return 1;
}

/* Opens up to `want` reads at indexes first.. on `addr`. A refusal after the
 * first only leaves fewer reads. Returns how many opened. */
static int open_reads(int first, int want, int addr, int bufsize)
{
    int i;

    for (i = 0; i < want; i++)
        if (!ep_open(first + i, addr, bufsize)) break;
    return i;
}

int hci_open(void)
{
    struct usb_fs_init init;
    int i;

    g_fd = open(BT_NODE, O_RDWR);
    if (g_fd < 0) {
        log_line("hci: open %s errno=%d", BT_NODE, errno);
        return 0;
    }

    memset(g_eps, 0, sizeof g_eps);
    for (i = 0; i < N_XFERS; i++) {
        g_bufptr[i][0] = g_buf[i];
        g_buflen[i][0] = XFER_BUF;
        g_eps[i].ppBuffer = g_bufptr[i];
        g_eps[i].pLength = g_buflen[i];
        g_eps[i].nFrames = 1;
        g_eps[i].flags = USB_FS_FLAG_SINGLE_SHORT_OK;
    }
    /* Bulk OUT must end a packet that is an exact multiple of the max packet
     * size with a zero-length packet. */
    g_eps[IX_ACL_OUT].flags = USB_FS_FLAG_FORCE_SHORT;

    memset(&init, 0, sizeof init);
    init.pEndpoints = g_eps;
    init.ep_index_max = N_XFERS;
    if (ioctl(g_fd, USB_FS_INIT, &init) != 0) {
        log_line("hci: USB_FS_INIT errno=%d", errno);
        hci_close();
        return 0;
    }

    /* The OUT transfer first, so it is never the one left out. */
    if (!ep_open(IX_ACL_OUT, EP_ACL_OUT, XFER_BUF)) {
        hci_close();
        return 0;
    }
    g_event_reads = open_reads(0, EVENT_READS, EP_EVENT, EVENT_BUF);
    g_acl_reads = open_reads(EVENT_READS, ACL_READS, EP_ACL_IN, XFER_BUF);
    if (g_event_reads < 1 || g_acl_reads < 1) {
        hci_close();
        return 0;
    }

    memset(g_pending, 0, sizeof g_pending);
    memset(&g_events, 0, sizeof g_events);
    memset(&g_acl, 0, sizeof g_acl);

    log_line("hci: conservative shared-controller mode, %d event and %d ACL read",
             g_event_reads, g_acl_reads);
    return 1;
}

void hci_close(void)
{
    struct usb_fs_uninit un;

    if (g_fd < 0) return;
    memset(&un, 0, sizeof un);
    ioctl(g_fd, USB_FS_UNINIT, &un);
    close(g_fd);
    g_fd = -1;
}

int hci_cmd(unsigned opcode, const void *params, int plen)
{
    struct usb_ctl_request req;
    unsigned char pkt[3 + 255];

    pkt[0] = (unsigned char)(opcode & 0xFF);
    pkt[1] = (unsigned char)(opcode >> 8);
    pkt[2] = (unsigned char)plen;
    if (plen > 0) memcpy(pkt + 3, params, (size_t)plen);

    memset(&req, 0, sizeof req);
    req.ucr_data = pkt;
    req.ucr_request.bmRequestType = UT_WRITE_CLASS_DEVICE;
    req.ucr_request.bRequest = 0;
    USETW(req.ucr_request.wValue, 0);
    USETW(req.ucr_request.wIndex, 0);
    USETW(req.ucr_request.wLength, 3 + plen);

    if (ioctl(g_fd, USB_DO_REQUEST, &req) != 0) {
        log_line("hci: command %#06x failed, errno=%d", opcode, errno);
        return 0;
    }
    return 1;
}

/* A read ends on a short packet, so it holds whole HCI packets: events are a
 * 2-byte header plus length, ACL packets a 4-byte one. A partial remainder
 * belongs to a packet whose rest went to the system's driver. */
static void split_packets(int index)
{
    const unsigned char *p = g_buf[index];
    int n = (int)g_buflen[index][0], off = 0;
    int event = is_event_read(index);

    for (;;) {
        int hdr = event ? 2 : 4, need;

        if (off + hdr > n) break;
        need = event ? hdr + p[off + 1] : hdr + (int)le16(p + off + 2);
        if (off + need > n) break;
        queue_push(event ? &g_events : &g_acl, p + off, need);
        off += need;
    }
}

static void on_complete(int index)
{
    g_pending[index] = 0;
    if (index == IX_ACL_OUT) {
        if (g_eps[index].status != 0)
            log_line("hci: ACL out status=%u", (unsigned)g_eps[index].status);
        return;
    }
    if (g_eps[index].status != 0 || g_eps[index].aFrames == 0) return;
    split_packets(index);
}

/* Handles every finished transfer. Returns 1 if there was any. */
static int reap(void)
{
    struct usb_fs_complete comp;
    int got = 0;

    for (;;) {
        memset(&comp, 0, sizeof comp);
        if (ioctl(g_fd, USB_FS_COMPLETE, &comp) != 0) break;
        if (comp.ep_index < N_XFERS) on_complete((int)comp.ep_index);
        got = 1;
    }
    return got;
}

int hci_pump(int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;

    for (;;) {
        int i, got;

        for (i = 0; i < g_event_reads; i++)
            if (!g_pending[i]) ep_start(i, EVENT_BUF);
        for (i = EVENT_READS; i < EVENT_READS + g_acl_reads; i++)
            if (!g_pending[i]) ep_start(i, XFER_BUF);

        got = reap();
        if (g_events.count || g_acl.count) return 1;
        if (got) continue;
        if (now_ms() >= deadline) return 0;
        usleep(1000);
    }
}

int hci_acl_send(const unsigned char *pkt, int len)
{
    long deadline = now_ms() + 1000;

    if (len > XFER_BUF) {
        log_line("hci: packet of %d bytes exceeds the transfer buffer", len);
        return 0;
    }
    while (g_pending[IX_ACL_OUT]) {
        reap();
        if (!g_pending[IX_ACL_OUT]) break;
        if (now_ms() >= deadline) {
            log_line("hci: ACL out still busy after 1 s");
            return 0;
        }
        usleep(500);
    }
    memcpy(g_buf[IX_ACL_OUT], pkt, (size_t)len);
    return ep_start(IX_ACL_OUT, len);
}
