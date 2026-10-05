#include "capture.h"
#include "log.h"

#include <ps5/kernel.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define LIB_PATH  "/system/common/lib/libSceAvcap2.sprx"

/* libSceAvcap2 imports libSceIpmi, which a payload's host process does not
 * have loaded. Left unresolved, the first IPC call terminates the process
 * outright, without a signal, so it is loaded first. */
#define IPMI_PATH "/system/common/lib/libSceIpmi.sprx"

#define KIND_AUDIO 0

#define ERR_EMPTY     0x81950002u   /* nothing to read yet */
#define ERR_OVERRUN   0x81950004u   /* the writer lapped us; already resynced */
#define READ_NOWAIT   1u

int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv,
                             uint32_t flags, void *opt, int *res);

/* sceAvcap2OpenAudio's parameter block, laid out as Remote Play fills it.
 *
 * All zeros selects the recording mix: game audio only, muted whenever the
 * system UI is on screen. Source 2 is Remote Play's mix, which has the system
 * sounds and is never muted. Mode 0 asks for raw stereo float; the other
 * modes return encoded audio or 12 channels. */
typedef struct {
    uint64_t size;          /* sizeof, 0x38 */
    uint64_t unk08;         /* 0x18 */
    uint32_t unk10;
    uint32_t source;        /* 2: Remote Play */
    uint32_t mode;          /* 0: raw 48 kHz stereo float */
    uint32_t unk1c;         /* 0xffffffff */
    uint64_t unk20;
    uint32_t bitrate;       /* encoded modes only */
    uint32_t pad;
    uint64_t unk30;
} open_param;

_Static_assert(sizeof(open_param) == 0x38, "OpenAudio parameter block size");

typedef int (*init_fn)(void);
typedef int (*open_fn)(void **out, const void *param, uint8_t kind);
typedef int (*handle_fn)(void *h);
typedef int (*read_fn)(void *h, void *buf, size_t size, void *info,
                       uint32_t flags);

static init_fn   p_initialize, p_terminate;
static open_fn   p_open_audio;
static handle_fn p_start, p_stop, p_close;
static read_fn   p_read_audio;

static void *g_handle;
static int g_initialized, g_started;
static long g_overruns;

long capture_overruns(void)
{
    return g_overruns;
}

/* The module's exports are not found by name through sceKernelDlsym, so
 * they are resolved by NID. */
static void *resolve(int mod, const char *nid, const char *name)
{
    intptr_t p = kernel_dynlib_resolve(getpid(), (uint32_t)mod, nid);

    if (!p) log_line("capture: %s (%s) not found", name, nid);
    return (void *)p;
}

/* Initialize, OpenAudio and Start. Errors are logged only when they change,
 * so a retry loop does not flood the log. */
static int start_capture(void)
{
    static int last_err;
    open_param param;
    const char *what;
    int r;

    what = "Initialize";
    r = p_initialize();
    if (r < 0) goto fail;
    g_initialized = 1;

    what = "OpenAudio";
    memset(&param, 0, sizeof param);
    param.size = sizeof param;
    param.unk08 = 0x18;
    param.source = 2;
    param.mode = 0;
    param.unk1c = 0xffffffffu;
    r = p_open_audio(&g_handle, &param, KIND_AUDIO);
    if (r < 0 || !g_handle) {
        g_handle = NULL;
        goto fail;
    }

    what = "Start";
    r = p_start(g_handle);
    if (r < 0) goto fail;

    g_started = 1;
    last_err = 0;
    log_line("capture: running, %d Hz stereo", CAPTURE_RATE);
    return 1;

fail:
    if (r != last_err)
        log_line("capture: %s -> %#x, not available yet", what, (unsigned)r);
    last_err = r;
    capture_close();
    return 0;
}

int capture_open(void)
{
    int mod, res = 0, r;

    r = sceKernelLoadStartModule(IPMI_PATH, 0, NULL, 0, NULL, &res);
    if (r < 0) {
        log_line("capture: load %s -> %#x", IPMI_PATH, (unsigned)r);
        return -1;
    }
    mod = sceKernelLoadStartModule(LIB_PATH, 0, NULL, 0, NULL, &res);
    if (mod < 0) {
        log_line("capture: load %s -> %#x", LIB_PATH, (unsigned)mod);
        return -1;
    }

    p_initialize = (init_fn)resolve(mod, "svzPXluOz8U", "sceAvcap2Initialize");
    p_terminate  = (init_fn)resolve(mod, "gojqghDU+1Y", "sceAvcap2Terminate");
    p_open_audio = (open_fn)resolve(mod, "3nHpE7Dp5SE", "sceAvcap2OpenAudio");
    p_close      = (handle_fn)resolve(mod, "tR7gPe1i8hw", "sceAvcap2Close");
    p_start      = (handle_fn)resolve(mod, "8CGNCwBsItI", "sceAvcap2Start");
    p_stop       = (handle_fn)resolve(mod, "Z5dKK0xnQ8g", "sceAvcap2Stop");
    p_read_audio = (read_fn)resolve(mod, "WhsHggqdwcg", "sceAvcap2ReadAudio");
    if (!p_initialize || !p_terminate || !p_open_audio || !p_close ||
        !p_start || !p_stop || !p_read_audio)
        return -1;

    return start_capture();
}

int capture_read(float *buf, size_t size)
{
    unsigned char info[16];
    int r;

    if (!g_started) return -1;
    r = p_read_audio(g_handle, buf, size, info, READ_NOWAIT);
    if (r == (int)ERR_EMPTY) return 0;
    if (r == (int)ERR_OVERRUN) {
        /* The ring holds about a third of a second. The library has already
         * moved the read position up to the writer. */
        if (g_overruns++ < 5)
            log_line("capture: ring overrun, skipped to live audio");
        return 0;
    }
    if (r < 0) {
        /* The service writes 0x81950008 into the ring when it ends the
         * session, for example when the running game closes. */
        log_line("capture: ReadAudio -> %#x", (unsigned)r);
        return -1;
    }
    return r;
}

int capture_restart(void)
{
    capture_close();
    return start_capture();
}

void capture_close(void)
{
    if (g_started) p_stop(g_handle);
    if (g_handle) p_close(g_handle);
    if (g_initialized) p_terminate();
    g_started = g_initialized = 0;
    g_handle = NULL;
}
