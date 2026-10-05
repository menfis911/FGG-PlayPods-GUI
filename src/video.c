#include "video.h"
#include "log.h"

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    void *data;
    uint64_t reserved[3];
} video_buffer;

typedef struct {
    uint32_t res;
    uint32_t reserved0;
    uint64_t reserved1[5];
} video_status;

typedef struct {
    uint8_t reserved[80];
} video_attr;

extern int sceSystemServiceHideSplashScreen(void);
extern int sceVideoOutOpen(int user_id, int bus_type, int index, const void *param);
extern int sceVideoOutGetOutputStatus(int handle, video_status *status);
extern int sceVideoOutSetFlipRate(int handle, int rate);
extern int sceVideoOutSubmitFlip(int handle, int index, uint32_t flip_mode, int64_t flip_arg);
extern void sceVideoOutSetBufferAttribute2(video_attr *attr, uint64_t pixel_format,
                                            uint32_t tiling_mode, uint32_t width,
                                            uint32_t height, uint64_t aspect,
                                            uint32_t pitch, uint64_t reserved);
extern int sceVideoOutRegisterBuffers2(int handle, int start_index, int buffer_index,
                                       video_buffer *buffers, int count,
                                       video_attr *attr, int flags, void *arg);
extern int sceKernelAllocateMainDirectMemory(size_t size, size_t alignment,
                                             int memory_type, intptr_t *physical);
extern int sceKernelMapDirectMemory(void **address, size_t size, int protection,
                                    int flags, intptr_t physical, size_t alignment);
extern int sceKernelReleaseDirectMemory(intptr_t physical, size_t size);

#define VIDEO_ALIGN 0x20000
#define VIDEO_BYTES_PER_PIXEL 4
#define VIDEO_BUFFERS 2
#define TILE_W 512
#define TILE_H 128
#define TILE_SIZE (TILE_W * TILE_H)

#include "ps5_tilemap.inc"

static int g_vout = -1;
static intptr_t g_physical;
static void *g_memory;
static size_t g_memory_size;
static size_t g_buffer_size;
static int g_width;
static int g_height;
static int g_tiles_w;
static int g_tiles_h;
static size_t g_tiled_pixels;
static uint32_t *g_linear;
static gui_video_mode g_mode;
static unsigned g_frame;

static void choose_size(gui_video_mode requested, int output_res,
                        int *width, int *height, gui_video_mode *actual)
{
    if (requested == GUI_VIDEO_2160P ||
        (requested == GUI_VIDEO_AUTO && output_res == 2)) {
        *width = 3840;
        *height = 2160;
        *actual = GUI_VIDEO_2160P;
        return;
    }
    if (requested == GUI_VIDEO_1440P)
        log_line("video: 1440p requested; using verified 1080p path for now");
    *width = 1920;
    *height = 1080;
    *actual = GUI_VIDEO_1080P;
}

static void swizzle(const uint32_t *src, uint32_t *dst)
{
    for (int y = 0; y < g_height; ++y) {
        const uint16_t *map = PS5_tilemap[y % TILE_H];
        const uint32_t *row = src + (size_t)y * g_width;
        const int ty = y / TILE_H;
        for (int x = 0; x < g_width; ++x) {
            const int tx = x / TILE_W;
            const size_t base = (size_t)TILE_SIZE *
                                ((size_t)tx + (size_t)ty * g_tiles_w);
            dst[base + map[x % TILE_W]] = row[x];
        }
    }
}

int video_init(gui_video_mode mode)
{
    video_status status;
    video_attr attr;
    video_buffer buffers[VIDEO_BUFFERS];
    int width, height;
    gui_video_mode actual;
    int rc;

    if (g_vout >= 0) return 1;
    memset(&status, 0, sizeof(status));
    memset(&attr, 0, sizeof(attr));
    memset(buffers, 0, sizeof(buffers));

    rc = sceSystemServiceHideSplashScreen();
    if (rc < 0) {
        /* Payload Manager launches may not expose the splash service. */
        log_line("video: HideSplashScreen unavailable 0x%08x; continuing", rc);
    }

    g_vout = sceVideoOutOpen(0xff, 0, 0, NULL);
    if (g_vout < 0) {
        log_line("video: VideoOutOpen failed 0x%08x", g_vout);
        g_vout = -1;
        return 0;
    }

    rc = sceVideoOutGetOutputStatus(g_vout, &status);
    if (rc < 0) {
        log_line("video: GetOutputStatus failed 0x%08x", rc);
        video_shutdown();
        return 0;
    }

    choose_size(mode, (int)status.res, &width, &height, &actual);
    g_width = width;
    g_height = height;
    g_mode = actual;
    g_tiles_w = (width + TILE_W - 1) / TILE_W;
    g_tiles_h = (height + TILE_H - 1) / TILE_H;
    g_tiled_pixels = (size_t)g_tiles_w * (size_t)g_tiles_h * TILE_SIZE;
    g_buffer_size = g_tiled_pixels * VIDEO_BYTES_PER_PIXEL;
    g_memory_size = g_buffer_size * VIDEO_BUFFERS;

    rc = sceKernelAllocateMainDirectMemory(g_memory_size, VIDEO_ALIGN, 3, &g_physical);
    if (rc < 0) {
        log_line("video: AllocateMainDirectMemory failed 0x%08x", rc);
        video_shutdown();
        return 0;
    }

    rc = sceKernelMapDirectMemory(&g_memory, g_memory_size, 0x33, 0,
                                  g_physical, VIDEO_ALIGN);
    if (rc < 0) {
        log_line("video: MapDirectMemory failed 0x%08x", rc);
        video_shutdown();
        return 0;
    }

    sceVideoOutSetBufferAttribute2(&attr, 0x8000000022000000ULL, 0,
                                   (uint32_t)width, (uint32_t)height,
                                   0, 0, 0);

    buffers[0].data = g_memory;
    buffers[1].data = (uint8_t *)g_memory + g_buffer_size;

    rc = sceVideoOutRegisterBuffers2(g_vout, 0, 0, buffers, VIDEO_BUFFERS,
                                     &attr, 0, NULL);
    if (rc < 0) {
        log_line("video: RegisterBuffers2 failed 0x%08x", rc);
        video_shutdown();
        return 0;
    }

    rc = sceVideoOutSetFlipRate(g_vout, 0);
    if (rc < 0) {
        log_line("video: SetFlipRate failed 0x%08x", rc);
        video_shutdown();
        return 0;
    }

    g_linear = malloc((size_t)g_width * (size_t)g_height * sizeof(uint32_t));
    if (!g_linear) {
        log_line("video: linear framebuffer allocation failed");
        video_shutdown();
        return 0;
    }

    memset(g_linear, 0, (size_t)g_width * (size_t)g_height * sizeof(uint32_t));
    log_line("video: initialized %dx%d tiles=%dx%d", g_width, g_height,
             g_tiles_w, g_tiles_h);
    return 1;
}

uint32_t *video_framebuffer(void)
{
    return g_linear;
}

void video_clear(uint32_t color)
{
    if (!g_linear) return;
    const size_t n = (size_t)g_width * (size_t)g_height;
    for (size_t i = 0; i < n; ++i) g_linear[i] = color;
}

void video_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!g_linear || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_width) w = g_width - x;
    if (y + h > g_height) h = g_height - y;
    if (w <= 0 || h <= 0) return;

    for (int yy = 0; yy < h; ++yy) {
        uint32_t *row = g_linear + (size_t)(y + yy) * g_width + x;
        for (int xx = 0; xx < w; ++xx) row[xx] = color;
    }
}

/* Compact 5x7 ASCII font. UI text intentionally stays ASCII until a
 * proper Cyrillic font atlas is added. */
static const uint8_t font5x7[96][7] = {
    [32]={0,0,0,0,0,0,0}, [45]={0,0,0,31,0,0,0},
    [46]={0,0,0,0,0,12,12}, [48]={14,17,19,21,25,17,14},
    [49]={4,12,4,4,4,4,14}, [50]={14,17,1,6,8,16,31},
    [51]={30,1,1,14,1,1,30}, [52]={2,6,10,18,31,2,2},
    [53]={31,16,16,30,1,1,30}, [54]={6,8,16,30,17,17,14},
    [55]={31,1,2,4,8,8,8}, [56]={14,17,17,14,17,17,14},
    [57]={14,17,17,15,1,2,12},
    [65]={14,17,17,31,17,17,17}, [66]={30,17,17,30,17,17,30},
    [67]={14,17,16,16,16,17,14}, [68]={30,17,17,17,17,17,30},
    [69]={31,16,16,30,16,16,31}, [70]={31,16,16,30,16,16,16},
    [71]={14,17,16,23,17,17,14}, [72]={17,17,17,31,17,17,17},
    [73]={14,4,4,4,4,4,14}, [74]={7,2,2,2,18,18,12},
    [75]={17,18,20,24,20,18,17}, [76]={16,16,16,16,16,16,31},
    [77]={17,27,21,17,17,17,17}, [78]={17,25,21,19,17,17,17},
    [79]={14,17,17,17,17,17,14}, [80]={30,17,17,30,16,16,16},
    [81]={14,17,17,17,21,18,13}, [82]={30,17,17,30,20,18,17},
    [83]={15,16,16,14,1,1,30}, [84]={31,4,4,4,4,4,4},
    [85]={17,17,17,17,17,17,14}, [86]={17,17,17,17,17,10,4},
    [87]={17,17,17,21,21,21,10}, [88]={17,17,10,4,10,17,17},
    [89]={17,17,10,4,4,4,4}, [90]={31,1,2,4,8,16,31},
    [91]={14,8,8,8,8,8,14}, [93]={14,2,2,2,2,2,14},
    [95]={0,0,0,0,0,0,31}
};

static void draw_char(int x, int y, char ch, int scale, uint32_t color)
{
    unsigned c = (unsigned char)ch;
    if (c < 32 || c > 127) c = 63;
    const uint8_t *glyph = font5x7[c - 32];
    for (int gy = 0; gy < 7; ++gy)
        for (int gx = 0; gx < 5; ++gx)
            if (glyph[gy] & (1u << (4 - gx)))
                video_fill_rect(x + gx * scale, y + gy * scale,
                                scale, scale, color);
}

void video_draw_text(int x, int y, const char *text, int scale, uint32_t color)
{
    if (!text || scale < 1) return;
    for (; *text; ++text) {
        draw_char(x, y, *text, scale, color);
        x += 6 * scale;
    }
}

int video_present(void)
{
    if (g_vout < 0 || !g_linear || !g_memory) return 0;
    uint32_t *dst = (uint32_t *)((uint8_t *)g_memory +
                                 (size_t)(g_frame & 1u) * g_buffer_size);
    swizzle(g_linear, dst);
    if (sceVideoOutSubmitFlip(g_vout, (int)(g_frame & 1u), 1,
                              (int64_t)g_frame) < 0) {
        log_line("video: SubmitFlip failed");
        return 0;
    }
    ++g_frame;
    return 1;
}

int video_present_test_frame(unsigned frame)
{
    if (!g_linear) return 0;
    switch ((frame / 60u) % 4u) {
    case 0: video_clear(0x202030ffU); break;
    case 1: video_clear(0x183a5cffU); break;
    case 2: video_clear(0x163c2affU); break;
    default: video_clear(0x4a2020ffU); break;
    }
    return video_present();
}

int video_get_info(gui_video_info *info)
{
    if (!info || g_vout < 0) return 0;
    info->width = g_width;
    info->height = g_height;
    info->refresh_hz = 60;
    info->mode = g_mode;
    return 1;
}

void video_shutdown(void)
{
    free(g_linear);
    g_linear = NULL;

    if (g_physical && g_memory_size)
        sceKernelReleaseDirectMemory(g_physical, g_memory_size);

    g_physical = 0;
    g_memory = NULL;
    g_memory_size = 0;
    g_buffer_size = 0;
    g_tiled_pixels = 0;
    g_width = 0;
    g_height = 0;
    g_vout = -1;
    g_mode = GUI_VIDEO_AUTO;
}
