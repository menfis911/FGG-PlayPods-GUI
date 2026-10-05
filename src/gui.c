#include "gui.h"
#include "bt.h"
#include "log.h"
#include "pad.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

static gui_screen g_screen = GUI_SCREEN_MAIN;
static gui_status g_status = GUI_STATUS_STARTING;
static int g_ready;
static unsigned g_frame;
static gui_video_mode g_video_mode = GUI_VIDEO_AUTO;
static int g_menu;
static int g_device;
static gui_action g_action;

static const char *screen_name(gui_screen screen)
{
    switch (screen) {
    case GUI_SCREEN_MAIN: return "MAIN";
    case GUI_SCREEN_DEVICES: return "DEVICES";
    case GUI_SCREEN_SAVED: return "SAVED";
    case GUI_SCREEN_LOGS: return "LOGS";
    case GUI_SCREEN_SETTINGS: return "SETTINGS";
    default: return "?";
    }
}

static const char *status_name(gui_status status)
{
    switch (status) {
    case GUI_STATUS_STARTING: return "STARTING";
    case GUI_STATUS_SCANNING: return "SCANNING";
    case GUI_STATUS_CONNECTING: return "CONNECTING";
    case GUI_STATUS_PAIRING: return "PAIRING";
    case GUI_STATUS_CONNECTED: return "CONNECTED";
    case GUI_STATUS_STREAMING: return "STREAMING";
    case GUI_STATUS_ERROR: return "ERROR";
    case GUI_STATUS_STOPPED: return "STOPPED";
    default: return "?";
    }
}

static void gui_queue(gui_action action)
{
    if (g_action == GUI_ACTION_NONE) g_action = action;
}

static void gui_draw(void)
{
    gui_video_info info;
    char line[128];

    if (!video_get_info(&info)) return;

    video_clear(0x11131affU);
    video_fill_rect(0, 0, info.width, 110, 0x202838ffU);
    video_draw_text(60, 38, "FGG-PLAYPODS-GUI", 5, 0xf0f4ffffU);
    snprintf(line, sizeof(line), "STATUS: %s", status_name(g_status));
    video_draw_text(60, 78, line, 2, 0xb8c8d8ffU);

    if (g_screen == GUI_SCREEN_DEVICES) {
        video_draw_text(70, 150, "BLUETOOTH DEVICES", 4, 0xf0f4ffffU);
        snprintf(line, sizeof(line), "FOUND: %d", bt_device_count());
        video_draw_text(70, 190, line, 2, 0xb8c8d8ffU);

        for (int i = 0; i < bt_device_count() && i < 12; ++i) {
            const bt_device *d = bt_device_get(i);
            if (!d) continue;
            int y = 235 + i * 62;
            if (i == g_device)
                video_fill_rect(50, y - 12, info.width - 100, 50, 0x294b72ffU);
            snprintf(line, sizeof(line), "%02d  %s", i + 1,
                     d->name[0] ? d->name : "(unknown)");
            video_draw_text(75, y, line, 3, 0xf0f4ffffU);
            snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X",
                     d->addr[5], d->addr[4], d->addr[3],
                     d->addr[2], d->addr[1], d->addr[0]);
            video_draw_text(75, y + 27, line, 2, 0xa8b8c8ffU);
        }
        video_draw_text(70, info.height - 70,
                        "X CONNECT   SQUARE RESCAN   CIRCLE BACK",
                        2, 0xb8c8d8ffU);
    } else {
        static const char *items[] = {
            "DEVICES", "SAVED DEVICES", "SETTINGS"
        };
        video_draw_text(70, 160, "MENU", 4, 0xf0f4ffffU);
        for (int i = 0; i < 3; ++i) {
            int y = 230 + i * 80;
            if (i == g_menu)
                video_fill_rect(60, y - 14, 700, 60, 0x294b72ffU);
            video_draw_text(90, y, items[i], 4, 0xf0f4ffffU);
        }
        video_draw_text(70, info.height - 70,
                        "UP/DOWN SELECT   X OPEN   OPTIONS QUIT",
                        2, 0xb8c8d8ffU);
    }

    video_present();
}

static void gui_handle_button(pad_button button)
{
    if (g_screen == GUI_SCREEN_DEVICES) {
        switch (button) {
        case PAD_UP:
            if (g_device > 0) --g_device;
            break;
        case PAD_DOWN:
            if (g_device + 1 < bt_device_count()) ++g_device;
            break;
        case PAD_CROSS:
            if (bt_device_count() > 0) {
                log_line("gui: connect requested for device %d", g_device);
                g_status = GUI_STATUS_CONNECTING;
                gui_queue(GUI_ACTION_CONNECT);
            }
            break;
        case PAD_SQUARE:
            log_line("gui: scan requested");
            g_status = GUI_STATUS_SCANNING;
                    gui_queue(GUI_ACTION_SCAN);
            break;
        case PAD_CIRCLE:
            g_screen = GUI_SCREEN_MAIN;
            break;
        default:
            break;
        }
        return;
    }

    switch (button) {
    case PAD_UP:
        if (g_menu > 0) --g_menu;
        break;
    case PAD_DOWN:
        if (g_menu < 2) ++g_menu;
        break;
    case PAD_CROSS:
        if (g_menu == 0) {
            g_screen = GUI_SCREEN_DEVICES;
            g_status = GUI_STATUS_SCANNING;
            gui_queue(GUI_ACTION_SCAN);
        } else {
            log_line("gui: screen %s selected", screen_name((gui_screen)(g_menu + 1)));
            g_screen = (gui_screen)(g_menu + 1);
        }
        break;
    case PAD_OPTIONS:
        gui_queue(GUI_ACTION_EXIT);
        break;
    default:
        break;
    }
}

int gui_init(void)
{
    gui_video_info info;

    if (g_ready) return 1;
    g_screen = GUI_SCREEN_MAIN;
    g_status = GUI_STATUS_STARTING;
    g_frame = 0;
    g_menu = 0;
    g_device = 0;
    g_action = GUI_ACTION_NONE;
    g_ready = 1;

    log_line("gui: initialized, screen=%s", screen_name(g_screen));

    if (!video_init(g_video_mode)) {
        log_line("gui: video initialization failed");
        g_status = GUI_STATUS_ERROR;
        g_ready = 0;
        return 0;
    }

    if (video_get_info(&info))
        log_line("gui: display=%dx%d refresh=%d mode=%d",
                 info.width, info.height, info.refresh_hz, (int)info.mode);

    if (!pad_init())
        log_line("gui: DualSense input unavailable");

    gui_draw();
    return 1;
}

void gui_shutdown(void)
{
    if (!g_ready) return;
    g_status = GUI_STATUS_STOPPED;
    pad_shutdown();
    video_shutdown();
    log_line("gui: shutdown");
    g_ready = 0;
}

void gui_set_screen(gui_screen screen)
{
    if (!g_ready || screen < GUI_SCREEN_MAIN || screen > GUI_SCREEN_SETTINGS)
        return;
    g_screen = screen;
}

void gui_set_status(gui_status status)
{
    if (!g_ready) return;
    g_status = status;
}

gui_action gui_take_action(void)
{
    gui_action action = g_action;
    g_action = GUI_ACTION_NONE;
    return action;
}

int gui_selected_device(void)
{
    return g_device;
}

void gui_tick(void)
{
    if (!g_ready) return;
    pad_button button = pad_poll();
    if (button != PAD_NONE) gui_handle_button(button);

    /* Redraw at a modest rate; Bluetooth event processing remains responsive. */
    if ((g_frame++ % 3u) == 0) gui_draw();
}
