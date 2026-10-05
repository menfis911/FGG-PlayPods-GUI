#include "a2dp.h"
#include "bt.h"
#include "capture.h"
#include "log.h"
#include "sbc.h"
#include "util.h"

#include <string.h>
#include <sys/types.h>

#define PSM_AVDTP   0x0019
#define SIG_CID     0x0040      /* our end of AVDTP signaling */
#define MEDIA_CID   0x0042      /* our end of the media channel */
#define OUR_SEID    1

/* AVDTP signals. */
#define AVDTP_DISCOVER          0x01
#define AVDTP_GET_CAPABILITIES  0x02
#define AVDTP_SET_CONFIGURATION 0x03
#define AVDTP_OPEN              0x06
#define AVDTP_START             0x07
#define AVDTP_CLOSE             0x08

#define RTP_HEADER      13      /* RTP header and the SBC frame count */
#define MAX_BITPOOL     53      /* the usual "high quality" SBC setting */

/* The queue between capture and encoder, and how late it may run: more
 * than MAX_LAG_MS behind (a radio stall, or clock drift) and the oldest
 * audio is dropped down to KEEP_LAG_MS, so sound stays in time with the
 * picture. */
#define FIFO_FRAMES     (CAPTURE_RATE / 2)
#define MAX_LAG_MS      250
#define KEEP_LAG_MS     80

#define RETRY_MS        500     /* capture restart attempts */
#define STATUS_MS       5000    /* status line interval */

typedef struct {
    int rate;
    unsigned char cfg[4];       /* A2DP SBC codec information elements */
    int sbc_freq, sbc_mode, sbc_blocks, sbc_subbands, sbc_alloc, bitpool;
} sbc_choice;

static l2cap_chan g_sig = { "signaling", SIG_CID, 0, 0, 0, 0, 0, 0, 672 };
static l2cap_chan g_media = { "media", MEDIA_CID, 0, 0, 0, 0, 0, 0, 672 };

static unsigned char g_av_label;
static int g_av_ready;
static unsigned char g_av_rsp[256];
static int g_av_len;

static int g_sink_seid;
static unsigned char g_sink_sbc[4];
static sbc_choice g_sc;

/* Stereo s16 frames waiting to be encoded, at the SBC rate. */
static int16_t g_fifo[FIFO_FRAMES * 2];
static int g_fifo_len;

/* Resampler state: position of the next output frame between the previous
 * input frame and the current one, in input frames. */
static double g_rs_pos;
static float g_rs_prev[2];

/* ---- AVDTP signaling --------------------------------------------------- */

static void on_signal_frame(unsigned cid, const unsigned char *d, int len)
{
    (void)cid;
    if (len < 2) return;
    if ((d[0] >> 2) & 3) return;            /* fragmented: not used by sinks */

    if ((d[0] & 3) == 0) {
        /* A command from the headset: this source takes the lead and
         * supports none. */
        unsigned char rej[3];
        rej[0] = (unsigned char)((d[0] & 0xF0) | 0x03);
        rej[1] = d[1] & 0x3F;
        rej[2] = 0x19;                      /* NOT_SUPPORTED_COMMAND */
        bt_send(g_sig.dcid, rej, 3);
        return;
    }
    if ((d[0] >> 4) == g_av_label) {
        g_av_len = len < (int)sizeof g_av_rsp ? len : (int)sizeof g_av_rsp;
        memcpy(g_av_rsp, d, (size_t)g_av_len);
        g_av_ready = 1;
    }
}

static void on_media_frame(unsigned cid, const unsigned char *d, int len)
{
    (void)cid; (void)d; (void)len;          /* a sink sends nothing here */
}

/* Sends an AVDTP command and waits for its response. Returns the message
 * type (2 accept, 3 reject) or -1 if none came. A response can go to the
 * system's driver, so the command is asked again with a new label. */
static int avdtp_cmd(unsigned char signal, const unsigned char *params, int plen)
{
    unsigned char p[64];
    int attempt;

    for (attempt = 1; ; attempt++) {
        g_av_label = (unsigned char)((g_av_label + 1) & 0x0F);
        p[0] = (unsigned char)(g_av_label << 4);    /* single packet, command */
        p[1] = signal;
        if (plen > 0) memcpy(p + 2, params, (size_t)plen);

        g_av_ready = 0;
        if (!bt_send(g_sig.dcid, p, 2 + plen)) return -1;
        if (bt_wait(&g_av_ready, 2000)) break;
        if (attempt == 3 || bt_link_lost()) {
            log_line("avdtp: signal %#04x unanswered", signal);
            return -1;
        }
    }
    return g_av_rsp[0] & 3;
}

static int find_sbc_sink(void)
{
    unsigned char seids[16], sinks[16];
    int nseps = 0, i;

    if (avdtp_cmd(AVDTP_DISCOVER, NULL, 0) != 2) {
        log_line("avdtp: discover failed");
        return 0;
    }
    for (i = 2; i + 1 < g_av_len && nseps < 16; i += 2) {
        seids[nseps] = g_av_rsp[i] >> 2;
        sinks[nseps] = (g_av_rsp[i + 1] >> 3) & 1;
        nseps++;
    }

    for (i = 0; i < nseps; i++) {
        unsigned char s = (unsigned char)(seids[i] << 2);
        int j = 2;

        if (!sinks[i]) continue;
        if (avdtp_cmd(AVDTP_GET_CAPABILITIES, &s, 1) != 2) continue;

        while (j + 2 <= g_av_len) {
            int cat = g_av_rsp[j], clen = g_av_rsp[j + 1];
            const unsigned char *c = g_av_rsp + j + 2;
            if (j + 2 + clen > g_av_len) break;
            if (cat == 0x07 && clen >= 6 && c[1] == 0x00) {     /* SBC */
                g_sink_seid = seids[i];
                memcpy(g_sink_sbc, c + 2, 4);
                log_line("avdtp: SBC sink %d, capabilities %02x %02x, bitpool %u-%u",
                         g_sink_seid, c[2], c[3], c[4], c[5]);
                return 1;
            }
            j += 2 + clen;
        }
    }
    log_line("avdtp: the headset has no SBC sink");
    return 0;
}

/* Picks the best configuration the sink allows: 48 kHz (the capture's
 * rate), joint stereo, 16 blocks, 8 subbands, loudness allocation. */
static int choose_sbc(sbc_choice *ch)
{
    const unsigned char *c = g_sink_sbc;
    int maxbp = c[3], minbp = c[2];

    memset(ch, 0, sizeof *ch);

    if (c[0] & 0x10)      { ch->rate = 48000; ch->cfg[0] |= 0x10; ch->sbc_freq = SBC_FREQ_48000; }
    else if (c[0] & 0x20) { ch->rate = 44100; ch->cfg[0] |= 0x20; ch->sbc_freq = SBC_FREQ_44100; }
    else { log_line("sbc: the headset takes neither 44.1 nor 48 kHz"); return 0; }

    if (c[0] & 0x01)      { ch->cfg[0] |= 0x01; ch->sbc_mode = SBC_MODE_JOINT_STEREO; }
    else if (c[0] & 0x02) { ch->cfg[0] |= 0x02; ch->sbc_mode = SBC_MODE_STEREO; }
    else { log_line("sbc: the headset takes no stereo mode"); return 0; }

    if (c[1] & 0x10)      { ch->cfg[1] |= 0x10; ch->sbc_blocks = SBC_BLK_16; }
    else if (c[1] & 0x20) { ch->cfg[1] |= 0x20; ch->sbc_blocks = SBC_BLK_12; }
    else if (c[1] & 0x40) { ch->cfg[1] |= 0x40; ch->sbc_blocks = SBC_BLK_8; }
    else                  { ch->cfg[1] |= 0x80; ch->sbc_blocks = SBC_BLK_4; }

    if (c[1] & 0x04)      { ch->cfg[1] |= 0x04; ch->sbc_subbands = SBC_SB_8; }
    else                  { ch->cfg[1] |= 0x08; ch->sbc_subbands = SBC_SB_4; }

    if (c[1] & 0x01)      { ch->cfg[1] |= 0x01; ch->sbc_alloc = SBC_AM_LOUDNESS; }
    else                  { ch->cfg[1] |= 0x02; ch->sbc_alloc = SBC_AM_SNR; }

    ch->bitpool = maxbp < MAX_BITPOOL ? maxbp : MAX_BITPOOL;
    if (ch->bitpool < minbp) ch->bitpool = minbp;
    ch->cfg[2] = (unsigned char)minbp;
    ch->cfg[3] = (unsigned char)ch->bitpool;
    return 1;
}

static int configure_stream(const sbc_choice *sc)
{
    unsigned char p[16];
    unsigned char s = (unsigned char)(g_sink_seid << 2);

    p[0] = (unsigned char)(g_sink_seid << 2);
    p[1] = (unsigned char)(OUR_SEID << 2);
    p[2] = 0x01; p[3] = 0x00;                   /* media transport */
    p[4] = 0x07; p[5] = 0x06;                   /* media codec, 6 bytes */
    p[6] = 0x00;                                /* audio */
    p[7] = 0x00;                                /* SBC */
    memcpy(p + 8, sc->cfg, 4);

    if (avdtp_cmd(AVDTP_SET_CONFIGURATION, p, 12) != 2) {
        log_line("avdtp: configuration refused");
        return 0;
    }
    if (avdtp_cmd(AVDTP_OPEN, &s, 1) != 2) {
        log_line("avdtp: open refused");
        return 0;
    }
    if (!bt_open_channel(&g_media, PSM_AVDTP, on_media_frame)) return 0;
    if (avdtp_cmd(AVDTP_START, &s, 1) != 2) {
        log_line("avdtp: start refused");
        return 0;
    }
    log_line("avdtp: streaming SBC %d Hz, bitpool %d", sc->rate, sc->bitpool);
    return 1;
}

int a2dp_start(void)
{
    return bt_open_channel(&g_sig, PSM_AVDTP, on_signal_frame) &&
           find_sbc_sink() && choose_sbc(&g_sc) && configure_stream(&g_sc);
}

void a2dp_stop(void)
{
    unsigned char s = (unsigned char)(g_sink_seid << 2);

    if (bt_link_lost()) return;
    if (g_media.dcid && !g_media.closed) {
        avdtp_cmd(AVDTP_CLOSE, &s, 1);
        bt_close_channel(&g_media);
    }
    bt_close_channel(&g_sig);
}

/* ---- the audio queue --------------------------------------------------- */

static int16_t to_s16(float v)
{
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (int16_t)(v * 32767.0f);
}

static void fifo_push(int16_t l, int16_t r)
{
    if (g_fifo_len >= FIFO_FRAMES) return;
    g_fifo[2 * g_fifo_len] = l;
    g_fifo[2 * g_fifo_len + 1] = r;
    g_fifo_len++;
}

static void fifo_take(int frames)
{
    g_fifo_len -= frames;
    memmove(g_fifo, g_fifo + 2 * frames, (size_t)g_fifo_len * 4);
}

/* Queues captured 48 kHz float frames at `rate`: a straight conversion when
 * the headset runs at 48 kHz, linear interpolation otherwise. */
static void queue_capture(const float *in, int frames, int rate)
{
    double step = (double)CAPTURE_RATE / rate;
    int i;

    if (rate == CAPTURE_RATE) {
        for (i = 0; i < frames; i++)
            fifo_push(to_s16(in[2 * i]), to_s16(in[2 * i + 1]));
        return;
    }
    for (i = 0; i < frames; i++) {
        while (g_rs_pos < 1.0) {
            float t = (float)g_rs_pos;
            fifo_push(to_s16(g_rs_prev[0] + (in[2 * i] - g_rs_prev[0]) * t),
                      to_s16(g_rs_prev[1] + (in[2 * i + 1] - g_rs_prev[1]) * t));
            g_rs_pos += step;
        }
        g_rs_pos -= 1.0;
        g_rs_prev[0] = in[2 * i];
        g_rs_prev[1] = in[2 * i + 1];
    }
}

/* SBC frames of `framelen` bytes that fit one media packet, bounded by the
 * headset's L2CAP MTU and the controller's ACL buffer. */
static int frames_that_fit(size_t framelen)
{
    int room = (int)g_media.remote_mtu, n;

    if (bt_max_frame() - 4 < room) room = bt_max_frame() - 4;
    n = (room - RTP_HEADER) / (int)framelen;
    return n > 15 ? 15 : n;
}

/* ---- the stream -------------------------------------------------------- */

int a2dp_stream(int capturing)
{
    static float rec[CAPTURE_RECORD / sizeof(float)];
    const sbc_choice *sc = &g_sc;
    sbc_t sbc;
    size_t codesize, framelen;
    int frames_per_pkt, samples_per_frame, pkt_frames, pkts = 0, records = 0;
    int restarts = 0;
    long t0, last_status, sent_samples = 0, trimmed = 0;
    long retry_at = 0, silence_t0 = 0, silent = 0;
    long max_lag = (long)sc->rate * MAX_LAG_MS / 1000;
    long keep_lag = (long)sc->rate * KEEP_LAG_MS / 1000;
    uint16_t seq = 1;
    unsigned char pkt[1024];

    if (sbc_init(&sbc, 0) != 0) {
        log_line("sbc: init failed");
        return 0;
    }
    sbc.frequency = (uint8_t)sc->sbc_freq;
    sbc.mode = (uint8_t)sc->sbc_mode;
    sbc.subbands = (uint8_t)sc->sbc_subbands;
    sbc.blocks = (uint8_t)sc->sbc_blocks;
    sbc.allocation = (uint8_t)sc->sbc_alloc;
    sbc.bitpool = (uint8_t)sc->bitpool;
    sbc.endian = SBC_LE;

    codesize = sbc_get_codesize(&sbc);
    framelen = sbc_get_frame_length(&sbc);
    samples_per_frame = (int)(codesize / 4);
    frames_per_pkt = frames_that_fit(framelen);
    if (frames_per_pkt < 1) {
        log_line("sbc: a %zu-byte frame does not fit a packet", framelen);
        sbc_finish(&sbc);
        return 0;
    }
    pkt_frames = frames_per_pkt * samples_per_frame;
    log_line("sbc: %zu-byte frames, %d per packet (%d ms)", framelen,
             frames_per_pkt, pkt_frames * 1000 / sc->rate);

    notify("FGG-PlayPods: audio on the headset - switch it off to stop");
    t0 = last_status = now_ms();
    if (!capturing) silence_t0 = retry_at = t0;     /* nothing to capture yet */

    /* Runs until the headset goes away: switching it off ends the session. */
    while (!bt_link_lost() && !g_media.closed) {
        int n = 0, sent_one = 0;

        /* Everything the capture has ready. */
        while (capturing && (n = capture_read(rec, sizeof rec)) > 0) {
            queue_capture(rec, n / (int)(sizeof(float) * CAPTURE_CHANNELS), sc->rate);
            records++;
        }
        if (capturing && n < 0) {
            /* The service ended the capture (a game closed, another one is
             * starting): keep the headset fed with silence meanwhile. */
            capturing = 0;
            silence_t0 = now_ms();
            retry_at = silence_t0 + RETRY_MS;
            silent = 0;
            g_fifo_len = 0;
        }
        if (!capturing) {
            long due = (now_ms() - silence_t0) * sc->rate / 1000;
            while (silent < due) {
                fifo_push(0, 0);
                silent++;
            }
            if (now_ms() >= retry_at) {
                if (capture_restart()) {
                    capturing = 1;
                    restarts++;
                } else {
                    retry_at = now_ms() + RETRY_MS;
                }
            }
        }

        if (g_fifo_len > max_lag) {
            trimmed += g_fifo_len - keep_lag;
            fifo_take(g_fifo_len - (int)keep_lag);
        }

        while (g_fifo_len >= pkt_frames && bt_can_send()) {
            unsigned char *out = pkt + RTP_HEADER;
            int f;

            pkt[0] = 0x80;                      /* RTP version 2 */
            pkt[1] = 0x60;                      /* payload type 96 */
            put16be(pkt + 2, seq++);
            put32be(pkt + 4, (uint32_t)sent_samples);
            put32be(pkt + 8, 1);                /* SSRC */
            pkt[12] = (unsigned char)frames_per_pkt;

            for (f = 0; f < frames_per_pkt; f++) {
                ssize_t written = 0;
                ssize_t used = sbc_encode(&sbc, g_fifo + f * samples_per_frame * 2,
                                          codesize, out, framelen, &written);
                if (used <= 0 || written <= 0) {
                    log_line("sbc: encode failed (%zd, %zd)", used, written);
                    sbc_finish(&sbc);
                    return 0;
                }
                out += written;
            }
            fifo_take(pkt_frames);

            if (!bt_send(g_media.dcid, pkt, (int)(out - pkt))) {
                log_line("media: send failed after %d packets", pkts);
                goto done;
            }
            pkts++;
            sent_one = 1;
            sent_samples += pkt_frames;
        }

        if (now_ms() - last_status >= STATUS_MS) {
            log_line("stream: %d packets, %d capture records, queue %d ms, "
                     "trimmed %ld, overruns %ld, restarts %d, %s, credits %d, "
                     "completions assumed %ld, reports missing %ld", pkts, records,
                     g_fifo_len * 1000 / sc->rate, trimmed, capture_overruns(),
                     restarts, capturing ? "capturing" : "waiting for audio",
                     bt_credits(), bt_completions_assumed(), bt_reports_missing());
            last_status = now_ms();
        }

        bt_poll(sent_one ? 1 : 3);
        gui_tick();
    }

done:
    log_line("stream: ended after %ld s, %d packets, %d capture records, "
             "trimmed %ld", (now_ms() - t0) / 1000, pkts, records, trimmed);
    sbc_finish(&sbc);
    return pkts > 0;
}
