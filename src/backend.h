#ifndef FGG_BACKEND_H
#define FGG_BACKEND_H

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
    char error[160];
} backend_snapshot;

int backend_init(const char *key_path, const char *saved_path,
                 void (*tick)(void));
void backend_shutdown(void);
int backend_request_scan(void);
int backend_request_connect(const char *mac);
int backend_request_disconnect(void);
void backend_get_snapshot(backend_snapshot *out);
const char *backend_status_name(backend_status status);

#endif
