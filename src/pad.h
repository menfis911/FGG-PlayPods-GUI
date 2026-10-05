#ifndef PAD_H
#define PAD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAD_NONE = 0,
    PAD_UP,
    PAD_DOWN,
    PAD_LEFT,
    PAD_RIGHT,
    PAD_CROSS,
    PAD_CIRCLE,
    PAD_SQUARE,
    PAD_TRIANGLE,
    PAD_OPTIONS
} pad_button;

int pad_init(void);
void pad_shutdown(void);
pad_button pad_poll(void);

#ifdef __cplusplus
}
#endif

#endif
