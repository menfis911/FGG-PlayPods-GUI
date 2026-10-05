#include "backend.h"

#include "a2dp.h"
#include "capture.h"
#include "log.h"
#include "util.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    COMMAND_NONE = 0,
    COMMAND_SCAN,
    COMMAND_CONNECT
} backend_command;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_thread_started;
static int g_shutdown;
static backend_command g_command;
static backend_snapshot g_state;
static bt_device g_connect_device;
static char g_key_path[256];
static char g_saved_path[256];
static void (*g_tick)(void);

static void set_status(backend_status status, const char *error)
{
    pthread_mutex_lock(&g_lock);
    g_state.status = status;
    if (error) snprintf(g_state.error, sizeof g_state.error, "%s", error);
    else g_state.error[0] = '\0';
    pthread_mutex_unlock(&g_lock);
}

static void format_mac(const unsigned char addr[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static int parse_mac(const char *text, unsigned char addr[6])
{
    unsigned x[6];
    if (!text || sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x",
                        &x[5], &x[4], &x[3], &x[2], &x[1], &x[0]) != 6)
        return 0;
    for (int i = 0; i < 6; ++i) addr[i] = (unsigned char)x[i];
    return 1;
}

static void load_saved(void)
{
    FILE *f;
    unsigned char record[23];
    bt_device saved;

    pthread_mutex_lock(&g_lock);
    g_state.saved_valid = 0;
    memset(&g_state.saved, 0, sizeof g_state.saved);
    pthread_mutex_unlock(&g_lock);
    memset(&saved, 0, sizeof saved);
    f = fopen(g_key_path, "rb");
    if (!f) return;
    if (fread(record, 1, sizeof record, f) != sizeof record) {
        fclose(f);
        return;
    }
    fclose(f);

    saved.valid = 1;
    memcpy(saved.addr, record, 6);
    saved.rssi = -127;
    snprintf(saved.name, sizeof saved.name, "Saved Bluetooth device");

    f = fopen(g_saved_path, "rb");
    if (f) {
        if (fgets(saved.name, sizeof saved.name, f))
            saved.name[strcspn(saved.name, "\r\n")] = '\0';
        fclose(f);
    }

    pthread_mutex_lock(&g_lock);
    g_state.saved_valid = 1;
    g_state.saved = saved;
    pthread_mutex_unlock(&g_lock);
}

static void save_device(const bt_device *device)
{
    FILE *f = fopen(g_saved_path, "wb");
    if (f) {
        fprintf(f, "%s\n", device->name[0] ? device->name : "Saved Bluetooth device");
        fclose(f);
    } else {
        log_line("saved device: cannot write %s errno=%d", g_saved_path, errno);
    }
    load_saved();
}

static void copy_devices(void)
{
    bt_device devices[BT_MAX_DEVICES];
    int count = bt_device_count();
    if (count > BT_MAX_DEVICES) count = BT_MAX_DEVICES;
    for (int i = 0; i < count; ++i) {
        const bt_device *device = bt_device_get(i);
        if (device) devices[i] = *device;
    }
    pthread_mutex_lock(&g_lock);
    memcpy(g_state.devices, devices, (size_t)count * sizeof devices[0]);
    g_state.device_count = count;
    pthread_mutex_unlock(&g_lock);
}

static int should_shutdown(void)
{
    int value;
    pthread_mutex_lock(&g_lock);
    value = g_shutdown;
    pthread_mutex_unlock(&g_lock);
    return value;
}

static backend_command take_command(bt_device *device)
{
    backend_command command;
    pthread_mutex_lock(&g_lock);
    command = g_command;
    g_command = COMMAND_NONE;
    if (command == COMMAND_CONNECT) *device = g_connect_device;
    pthread_mutex_unlock(&g_lock);
    return command;
}

static int start_controller(void)
{
    set_status(BACKEND_STARTING, NULL);
    if (!bt_start()) {
        pthread_mutex_lock(&g_lock);
        g_state.controller_ready = 0;
        pthread_mutex_unlock(&g_lock);
        set_status(BACKEND_ERROR, "Bluetooth controller did not answer");
        return 0;
    }
    bt_set_tick(g_tick);
    pthread_mutex_lock(&g_lock);
    g_state.controller_ready = 1;
    pthread_mutex_unlock(&g_lock);
    set_status(BACKEND_READY, NULL);
    return 1;
}

static void stop_controller(void)
{
    bt_stop();
    pthread_mutex_lock(&g_lock);
    g_state.controller_ready = 0;
    g_state.active_valid = 0;
    pthread_mutex_unlock(&g_lock);
}

static void run_connection(const bt_device *device, int capturing)
{
    char mac[18];
    a2dp_clear_stop();
    bt_clear_stop();
    format_mac(device->addr, mac);
    log_line("backend: connecting to %s (%s)", device->name, mac);
    pthread_mutex_lock(&g_lock);
    g_state.active = *device;
    g_state.active_valid = 1;
    pthread_mutex_unlock(&g_lock);
    set_status(BACKEND_CONNECTING, NULL);

    if (!bt_connect_addr(device->addr, device->name, g_key_path)) {
        load_saved();
        if (!should_shutdown()) set_status(BACKEND_ERROR, "Bluetooth connection failed");
        return;
    }
    save_device(device);
    set_status(BACKEND_CONNECTED, NULL);
    if (!a2dp_start()) {
        set_status(BACKEND_ERROR, "A2DP setup failed");
        return;
    }
    set_status(BACKEND_STREAMING, NULL);
    a2dp_stream(capturing);
    set_status(BACKEND_DISCONNECTING, NULL);
    a2dp_stop();
}

static void *worker_main(void *unused)
{
    int capturing;
    int controller_ready;
    int scanning = 0;
    long scan_deadline = 0;
    (void)unused;

    capturing = capture_open();
    if (capturing < 0)
        log_line("audio capture unavailable; Web GUI remains usable");
    controller_ready = start_controller();

    while (!should_shutdown()) {
        bt_device device;
        backend_command command = take_command(&device);

        if (command == COMMAND_SCAN) {
            if (!controller_ready) controller_ready = start_controller();
            if (controller_ready && bt_scan_start()) {
                scanning = 1;
                scan_deadline = now_ms() + 16000;
                set_status(BACKEND_SCANNING, NULL);
            } else {
                set_status(BACKEND_ERROR, "Unable to start Bluetooth scan");
            }
        } else if (command == COMMAND_CONNECT) {
            if (scanning) {
                bt_scan_cancel();
                scanning = 0;
            }
            if (!controller_ready) controller_ready = start_controller();
            if (controller_ready) run_connection(&device, capturing);
            if (controller_ready) {
                stop_controller();
                controller_ready = 0;
            }
            if (!should_shutdown()) controller_ready = start_controller();
        }

        if (controller_ready && scanning) {
            int done = bt_scan_poll();
            copy_devices();
            if (done || now_ms() >= scan_deadline) {
                if (!done) bt_scan_cancel();
                scanning = 0;
                set_status(BACKEND_READY, NULL);
            }
        } else {
            usleep(20000);
        }
    }

    if (scanning) bt_scan_cancel();
    if (controller_ready) stop_controller();
    if (capturing >= 0) capture_close();
    set_status(BACKEND_STOPPED, NULL);
    return NULL;
}

int backend_init(const char *key_path, const char *saved_path,
                 void (*tick)(void))
{
    memset(&g_state, 0, sizeof g_state);
    g_state.status = BACKEND_STARTING;
    snprintf(g_key_path, sizeof g_key_path, "%s", key_path);
    snprintf(g_saved_path, sizeof g_saved_path, "%s", saved_path);
    g_tick = tick;
    g_shutdown = 0;
    g_command = COMMAND_NONE;
    load_saved();
    if (pthread_create(&g_thread, NULL, worker_main, NULL) != 0) return 0;
    g_thread_started = 1;
    return 1;
}

void backend_shutdown(void)
{
    if (!g_thread_started) return;
    pthread_mutex_lock(&g_lock);
    g_shutdown = 1;
    pthread_mutex_unlock(&g_lock);
    a2dp_request_stop();
    bt_request_stop();
    pthread_join(g_thread, NULL);
    g_thread_started = 0;
}

int backend_request_scan(void)
{
    int accepted = 0;
    pthread_mutex_lock(&g_lock);
    if (g_command == COMMAND_NONE &&
        (g_state.status == BACKEND_READY || g_state.status == BACKEND_ERROR)) {
        g_command = COMMAND_SCAN;
        accepted = 1;
    }
    pthread_mutex_unlock(&g_lock);
    return accepted;
}

int backend_request_connect(const char *mac)
{
    unsigned char addr[6];
    int accepted = 0;
    if (!parse_mac(mac, addr)) return 0;

    pthread_mutex_lock(&g_lock);
    if (g_command == COMMAND_NONE &&
        (g_state.status == BACKEND_READY || g_state.status == BACKEND_ERROR)) {
        for (int i = 0; i < g_state.device_count; ++i) {
            if (memcmp(g_state.devices[i].addr, addr, 6) == 0) {
                g_connect_device = g_state.devices[i];
                accepted = 1;
                break;
            }
        }
        if (!accepted && g_state.saved_valid &&
            memcmp(g_state.saved.addr, addr, 6) == 0) {
            g_connect_device = g_state.saved;
            accepted = 1;
        }
        if (accepted) g_command = COMMAND_CONNECT;
    }
    pthread_mutex_unlock(&g_lock);
    return accepted;
}

int backend_request_disconnect(void)
{
    int accepted = 0;
    pthread_mutex_lock(&g_lock);
    if (g_state.status == BACKEND_CONNECTING ||
        g_state.status == BACKEND_CONNECTED ||
        g_state.status == BACKEND_STREAMING) {
        g_state.status = BACKEND_DISCONNECTING;
        accepted = 1;
    }
    pthread_mutex_unlock(&g_lock);
    if (accepted) {
        a2dp_request_stop();
        bt_request_stop();
    }
    return accepted;
}

void backend_get_snapshot(backend_snapshot *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_state;
    pthread_mutex_unlock(&g_lock);
}

const char *backend_status_name(backend_status status)
{
    switch (status) {
    case BACKEND_STARTING: return "starting";
    case BACKEND_READY: return "ready";
    case BACKEND_SCANNING: return "scanning";
    case BACKEND_CONNECTING: return "connecting";
    case BACKEND_CONNECTED: return "connected";
    case BACKEND_STREAMING: return "streaming";
    case BACKEND_DISCONNECTING: return "disconnecting";
    case BACKEND_ERROR: return "error";
    case BACKEND_STOPPED: return "stopped";
    default: return "unknown";
    }
}
