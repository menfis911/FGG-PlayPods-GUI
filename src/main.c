/* FGG-PlayPods-GUI -- native Bluetooth/A2DP backend with a Web GUI. */
#include "backend.h"
#include "http_server.h"
#include "log.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

extern void sceKernelSetProcessName(const char *name);

#ifndef FGG_VERSION
#define FGG_VERSION "dev"
#endif

#define STATE_DIR  "/data/fgg-playpods-gui"
#define LOG_PATH   STATE_DIR "/gui-playpods.log"
#define LOCK_PATH  STATE_DIR "/gui-playpods.lock"
#define KEY_PATH   STATE_DIR "/paired.key"
#define SAVED_PATH STATE_DIR "/saved-device.txt"
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
    sceKernelSetProcessName("fgg-playpods-web.elf");

    if (!log_open(STATE_DIR, LOG_PATH)) return 1;
    log_line("========================================");
    log_line("FGG-PlayPods-GUI %s (Web GUI)", FGG_VERSION);

    if (!lock_take()) {
        notify("FGG-PlayPods-GUI: already running");
        log_line("another backend instance holds the lock");
        log_close();
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    if (!backend_init(KEY_PATH, SAVED_PATH, lock_refresh)) {
        log_line("backend worker initialization failed");
        goto out;
    }

    notify("FGG-PlayPods-GUI: Web GUI ready on port %d", HTTP_PORT);
    result = http_server_run(HTTP_PORT, &g_running) ? 0 : 1;
    g_running = 0;
    backend_shutdown();

out:
    lock_release();
    log_close();
    return result;
}
