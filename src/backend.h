#ifndef FGG_BACKEND_H
#define FGG_BACKEND_H

#include "a2dp.h"
#include "bt.h"

typedef enum {
    BACKEND_STARTING = 0,
    BACKEND_READY,
    BACKEND_SCANNING,
    BACKEND_CONNECTING,
    BACKEND_CONNECTED,
    BACKEND_STREAMING,
    BACKEND_DISCONNECTING,
    BACKEND_ERROR,
    BACKEND_STOPPED
} backend_status;

typedef struct {
    backend_status status;
    int controller_ready;
    int device_count;
    bt_device devices[BT_MAX_DEVICES];
    int saved_valid;
    bt_device saved;
    int active_valid;
    bt_device active;
    a2dp_metrics audio;
    a2dp_profile selected_profile;
    unsigned state_revision;
    unsigned devices_revision;
    unsigned saved_revision;
    char error[160];
} backend_snapshot;

int backend_init(const char *key_path, const char *saved_path,
                 const char *profile_path,
                 void (*tick)(void));
void backend_shutdown(void);
int backend_request_scan(void);
int backend_request_connect(const char *mac);
int backend_request_disconnect(void);
int backend_set_audio_profile(const char *profile);
void backend_get_snapshot(backend_snapshot *out);
const char *backend_status_name(backend_status status);

#endif
