#include "pad.h"
#include "log.h"

#include <stdint.h>
#include <string.h>

typedef int32_t SceUserServiceUserId;

typedef struct {
    uint64_t timestamp;
    uint32_t buttons;
    uint8_t left_x;
    uint8_t left_y;
    uint8_t right_x;
    uint8_t right_y;
    uint8_t l2;
    uint8_t r2;
    uint8_t reserved[106];
    uint8_t connected;
} pad_data;

/*
 * Button values match the normal PS5 ScePadData contract.  Keep these local
 * so the project does not depend on unreleased/private Sony headers.
 */
#define PAD_BUTTON_UP        0x00000010U
#define PAD_BUTTON_RIGHT     0x00000020U
#define PAD_BUTTON_DOWN      0x00000040U
#define PAD_BUTTON_LEFT      0x00000080U
#define PAD_BUTTON_L1        0x00000100U
#define PAD_BUTTON_R1        0x00000200U
#define PAD_BUTTON_L2        0x00000400U
#define PAD_BUTTON_R2        0x00000800U
#define PAD_BUTTON_TRIANGLE  0x00001000U
#define PAD_BUTTON_CIRCLE    0x00002000U
#define PAD_BUTTON_CROSS     0x00004000U
#define PAD_BUTTON_SQUARE    0x00008000U
#define PAD_BUTTON_OPTIONS   0x00020000U

extern int scePadInit(void);
extern int scePadOpen(SceUserServiceUserId user_id, int type, int index, void *param);
extern int scePadReadState(int handle, void *data);
extern int scePadClose(int handle);

extern int sceUserServiceInitialize(void *param);
extern int sceUserServiceGetInitialUser(SceUserServiceUserId *user_id);

static int g_pad = -1;
static uint32_t g_previous;

static pad_button decode_button(uint32_t buttons)
{
    if (buttons & PAD_BUTTON_UP) return PAD_UP;
    if (buttons & PAD_BUTTON_DOWN) return PAD_DOWN;
    if (buttons & PAD_BUTTON_LEFT) return PAD_LEFT;
    if (buttons & PAD_BUTTON_RIGHT) return PAD_RIGHT;
    if (buttons & PAD_BUTTON_CROSS) return PAD_CROSS;
    if (buttons & PAD_BUTTON_CIRCLE) return PAD_CIRCLE;
    if (buttons & PAD_BUTTON_SQUARE) return PAD_SQUARE;
    if (buttons & PAD_BUTTON_TRIANGLE) return PAD_TRIANGLE;
    if (buttons & PAD_BUTTON_OPTIONS) return PAD_OPTIONS;
    return PAD_NONE;
}

int pad_init(void)
{
    SceUserServiceUserId user_id;
    int rc;

    if (g_pad >= 0) return 1;

    rc = sceUserServiceInitialize(NULL);
    if (rc < 0 && rc != 0x80960003) {
        log_line("pad: UserServiceInitialize failed 0x%08x", rc);
        return 0;
    }

    rc = sceUserServiceGetInitialUser(&user_id);
    if (rc < 0) {
        log_line("pad: GetInitialUser failed 0x%08x", rc);
        return 0;
    }

    rc = scePadInit();
    if (rc < 0) {
        log_line("pad: PadInit failed 0x%08x", rc);
        return 0;
    }

    g_pad = scePadOpen(user_id, 0, 0, NULL);
    if (g_pad < 0) {
        log_line("pad: PadOpen failed 0x%08x", g_pad);
        g_pad = -1;
        return 0;
    }

    g_previous = 0;
    log_line("pad: DualSense opened");
    return 1;
}

pad_button pad_poll(void)
{
    pad_data data;
    uint32_t changed;

    if (g_pad < 0) return PAD_NONE;

    memset(&data, 0, sizeof(data));
    if (scePadReadState(g_pad, &data) < 0)
        return PAD_NONE;

    changed = data.buttons & ~g_previous;
    g_previous = data.buttons;
    return decode_button(changed);
}

void pad_shutdown(void)
{
    if (g_pad >= 0) {
        scePadClose(g_pad);
        g_pad = -1;
    }
    g_previous = 0;
}
