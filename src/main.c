/* AudioBridge-GUI -- native Bluetooth/A2DP backend with a Web GUI. */
#include "backend.h"
#include "http_server.h"
#include "log.h"
#include "tile.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

extern void sceKernelSetProcessName(const char *name);

#ifndef AUDIOBRIDGE_VERSION
#define AUDIOBRIDGE_VERSION "dev"
#endif

/* Kept stable so upgrades retain existing pairing keys and saved devices. */
#define STATE_DIR  "/data/fgg-playpods-gui"
#define LOG_PATH   STATE_DIR "/gui-playpods.log"
#define LOCK_PATH  STATE_DIR "/gui-playpods.lock"
#define KEY_PATH   STATE_DIR "/paired.key"
#define SAVED_PATH STATE_DIR "/saved-device.txt"
#define PROFILE_PATH STATE_DIR "/audio-profile.txt"
#define HTTP_PORT  18195
#define LOCK_STALE_SECONDS 60

static volatile int g_running = 1;

static void on_signal(int signal_number)
{
    (void)signal_number;
    g_running = 0;
}

static int lock_take(void)
{
    struct stat stv;
    int fd;
    if (stat(LOCK_PATH, &stv) == 0 &&
        time(NULL) - stv.st_mtime < LOCK_STALE_SECONDS) return 0;
    fd = open(LOCK_PATH, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

static void lock_refresh(void) { utimes(LOCK_PATH, NULL); }
static void lock_release(void) { unlink(LOCK_PATH); }

int main(void)
{
    int result = 1;
    sceKernelSetProcessName("audiobridge.elf");

    if (!log_open(STATE_DIR, LOG_PATH)) return 1;
    log_line("========================================");
    log_line("AudioBridge-GUI %s (Web GUI)", AUDIOBRIDGE_VERSION);

    if (!lock_take()) {
        notify("AudioBridge-GUI: already running");
        log_line("another backend instance holds the lock");
        log_close();
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    if (tile_install_or_update(AUDIOBRIDGE_VERSION) < 0)
        log_line("tile installation unavailable; backend will continue");
    if (!backend_init(KEY_PATH, SAVED_PATH, PROFILE_PATH, lock_refresh)) {
        log_line("backend worker initialization failed");
        goto out;
    }

    notify("AudioBridge-GUI: Web GUI ready on port %d", HTTP_PORT);
    result = http_server_run(HTTP_PORT, &g_running) ? 0 : 1;
    g_running = 0;
    backend_shutdown();

out:
    lock_release();
    log_close();
    return result;
}
