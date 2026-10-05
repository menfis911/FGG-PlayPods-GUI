/* FGG-PlayPods-GUI -- PS5 Bluetooth audio payload with interactive GUI. */
#include "a2dp.h"
#include "bt.h"
#include "capture.h"
#include "gui.h"
#include "log.h"

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define VERSION   "0.1.2"
#define STATE_DIR "/data/fgg-playpods-gui"
#define LOG_PATH  STATE_DIR "/gui-playpods.log"
#define LOCK_PATH STATE_DIR "/gui-playpods.lock"
#define KEY_PATH  STATE_DIR "/paired.key"
#define LOCK_STALE_SECONDS 15

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
    int capturing = 0;
    int running = 1;

    if (!log_open(STATE_DIR, LOG_PATH)) return 1;
    if (!gui_init()) {
        log_line("gui initialization failed");
        log_close();
        return 1;
    }

    log_line("========================================");
    log_line("FGG-PlayPods-GUI %s", VERSION);
    notify("FGG-PlayPods-GUI started");

    if (!lock_take()) {
        notify("FGG-PlayPods-GUI: already running");
        gui_set_status(GUI_STATUS_ERROR);
        gui_shutdown();
        log_close();
        return 1;
    }
    bt_set_tick(lock_refresh);

    capturing = capture_open();
    if (capturing < 0)
        log_line("audio capture unavailable; Bluetooth GUI remains usable");

    if (!bt_start()) {
        notify("FGG-PlayPods-GUI: Bluetooth controller did not answer");
        gui_set_status(GUI_STATUS_ERROR);
        goto out;
    }

    /* The GUI now owns the session. Nothing connects automatically. */
    gui_set_status(GUI_STATUS_STARTING);

    while (running) {
        gui_action action;

        /* GUI owns the display/input loop. Keep it ticking continuously. */
        gui_tick();
        bt_poll(16);
        action = gui_take_action();

        if (action == GUI_ACTION_EXIT) {
            running = 0;
            continue;
        }

        if (action == GUI_ACTION_SCAN) {
            if (bt_scan_start()) {
                gui_set_status(GUI_STATUS_SCANNING);
            } else {
                gui_set_status(GUI_STATUS_ERROR);
            }
            continue;
        }

        if (action == GUI_ACTION_CONNECT) {
            int index = gui_selected_device();
            gui_set_status(GUI_STATUS_CONNECTING);
            if (!bt_connect_device(index, KEY_PATH)) {
                gui_set_status(GUI_STATUS_ERROR);
                continue;
            }

            gui_set_status(GUI_STATUS_CONNECTED);
            if (!a2dp_start()) {
                gui_set_status(GUI_STATUS_ERROR);
                bt_stop();
                continue;
            }

            gui_set_status(GUI_STATUS_STREAMING);
            a2dp_stream(capturing);
            a2dp_stop();
            bt_stop();
            gui_set_status(GUI_STATUS_STARTING);
        }
    }

    bt_stop();

out:
    if (capturing > 0) capture_close();
    gui_shutdown();
    lock_release();
    log_close();
    return 0;
}
