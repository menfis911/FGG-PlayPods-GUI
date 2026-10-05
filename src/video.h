#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GUI_VIDEO_AUTO = 0,
    GUI_VIDEO_1080P,
    GUI_VIDEO_1440P,
    GUI_VIDEO_2160P
} gui_video_mode;

typedef struct {
    int width;
    int height;
    int refresh_hz;
    gui_video_mode mode;
} gui_video_info;

int video_init(gui_video_mode mode);
void video_shutdown(void);
int video_present_test_frame(unsigned frame);
int video_get_info(gui_video_info *info);

/* Linear framebuffer drawing API. video_present() performs the PS5 tile
 * swizzle before submitting the frame. */
uint32_t *video_framebuffer(void);
void video_clear(uint32_t color);
void video_fill_rect(int x, int y, int w, int h, uint32_t color);
void video_draw_text(int x, int y, const char *text, int scale, uint32_t color);
int video_present(void);

#ifdef __cplusplus
}
#endif

#endif
