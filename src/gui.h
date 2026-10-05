#ifndef GUI_H
#define GUI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GUI_SCREEN_MAIN = 0,
    GUI_SCREEN_DEVICES,
    GUI_SCREEN_SAVED,
    GUI_SCREEN_LOGS,
    GUI_SCREEN_SETTINGS
} gui_screen;

typedef enum {
    GUI_STATUS_STARTING = 0,
    GUI_STATUS_SCANNING,
    GUI_STATUS_CONNECTING,
    GUI_STATUS_PAIRING,
    GUI_STATUS_CONNECTED,
    GUI_STATUS_STREAMING,
    GUI_STATUS_ERROR,
    GUI_STATUS_STOPPED
} gui_status;

typedef enum {
    GUI_ACTION_NONE = 0,
    GUI_ACTION_SCAN,
    GUI_ACTION_CONNECT,
    GUI_ACTION_EXIT
} gui_action;

int gui_init(void);
void gui_shutdown(void);
void gui_set_screen(gui_screen screen);
void gui_set_status(gui_status status);
void gui_tick(void);

gui_action gui_take_action(void);
int gui_selected_device(void);

#ifdef __cplusplus
}
#endif

#endif
