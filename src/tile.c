/* Dashboard tile installation follows ps5-payload-dev/tv4play's
 * sceAppInstUtil flow. AudioBridge uses the direct deeplink metadata shape
 * also used by ps5-payload-dev/websrv, so the tile does not need websrv. */
#include "tile.h"

#include "log.h"

#include <ps5/kernel.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TILE_ROOT       "/user/app/" AUDIOBRIDGE_TITLE_ID
#define TILE_SCE_SYS    TILE_ROOT "/sce_sys"
#define TILE_PARAM      TILE_SCE_SYS "/param.json"
#define TILE_ICON       TILE_SCE_SYS "/icon0.png"
#define TILE_MARKER     TILE_ROOT "/audiobridge-owner.txt"
#define TILE_APPMETA    "/user/appmeta/" AUDIOBRIDGE_TITLE_ID "/param.json"
#define MARKER_PREFIX   "AudioBridge-GUI\n"

#define INCASSET(name, file)                    \
    __asm__(".section .rodata\n"                 \
            ".global " #name "\n"               \
            ".global " #name "_end\n"           \
            ".global " #name "_size\n"          \
            ".align 16\n"                        \
            #name ":\n"                          \
            ".incbin \"" file "\"\n"             \
            #name "_end:\n"                      \
            #name "_size:\n"                     \
            ".quad " #name "_end - " #name "\n" \
            ".previous\n");                     \
    extern const uint8_t name[];                \
    extern const size_t name##_size

INCASSET(tile_param_json, "tile/sce_sys/param.json");
INCASSET(tile_icon_png, "sce_sys/icon0.png");

int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallAll(void *);
int sceAppInstUtilAppUnInstall(const char *);

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int marker_read(char *buf, size_t size)
{
    FILE *f = fopen(TILE_MARKER, "rb");
    size_t n;
    if (!f) return 0;
    n = fread(buf, 1, size - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strncmp(buf, MARKER_PREFIX, strlen(MARKER_PREFIX)) == 0;
}

static int write_atomic(const char *path, const void *data, size_t size)
{
    char temporary[320];
    FILE *f;
    if (snprintf(temporary, sizeof temporary, "%s.tmp", path) >=
        (int)sizeof temporary) return -1;
    f = fopen(temporary, "wb");
    if (!f) return -1;
    if (size && fwrite(data, 1, size, f) != size) {
        fclose(f);
        unlink(temporary);
        return -1;
    }
    if (fclose(f) != 0 || rename(temporary, path) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

static int install_app(void)
{
    int (*install_title_dir)(const char *, const char *, void *) = NULL;
    const char *nid = "Wudg3Xe3heE";
    uint32_t handle;

    if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle))
        install_title_dir = (void *)kernel_dynlib_resolve(-1, handle, nid);
    if (install_title_dir)
        return install_title_dir(AUDIOBRIDGE_TITLE_ID, "/user/app/", NULL);
    return sceAppInstUtilAppInstallAll(NULL);
}

int tile_install_or_update(const char *version)
{
    char marker[160];
    char expected[160];
    int root_exists = path_exists(TILE_ROOT);
    int owned = marker_read(marker, sizeof marker);
    int err;

    snprintf(expected, sizeof expected, MARKER_PREFIX "%s\n", version);
    if (root_exists && !owned) {
        log_line("tile: title-id collision at %s; refusing to overwrite",
                 TILE_ROOT);
        notify("AudioBridge: tile ID %s is already in use", AUDIOBRIDGE_TITLE_ID);
        return -1;
    }
    if (!root_exists && path_exists(TILE_APPMETA)) {
        log_line("tile: registered title-id collision for %s; refusing to overwrite",
                 AUDIOBRIDGE_TITLE_ID);
        notify("AudioBridge: tile ID %s is already registered", AUDIOBRIDGE_TITLE_ID);
        return -1;
    }
    if (owned && strcmp(marker, expected) == 0 &&
        path_exists(TILE_PARAM) && path_exists(TILE_ICON) &&
        path_exists(TILE_APPMETA)) {
        log_line("tile: %s already installed for version %s",
                 AUDIOBRIDGE_TITLE_ID, version);
        return 0;
    }

    if ((err = sceAppInstUtilInitialize()) != 0) {
        log_line("tile: sceAppInstUtilInitialize -> %#x", (unsigned)err);
        return -1;
    }
    if ((mkdir(TILE_ROOT, 0755) != 0 && errno != EEXIST) ||
        (mkdir(TILE_SCE_SYS, 0755) != 0 && errno != EEXIST)) {
        log_line("tile: cannot create app directories errno=%d", errno);
        return -1;
    }

    /* Establish ownership first. A later interrupted update remains safely
     * recoverable by the next payload run or by the dedicated uninstaller. */
    if (write_atomic(TILE_MARKER, expected, strlen(expected)) != 0 ||
        write_atomic(TILE_PARAM, tile_param_json, tile_param_json_size) != 0 ||
        write_atomic(TILE_ICON, tile_icon_png, tile_icon_png_size) != 0) {
        log_line("tile: cannot write launcher files errno=%d", errno);
        return -1;
    }
    if ((err = install_app()) != 0) {
        log_line("tile: install %s -> %#x", AUDIOBRIDGE_TITLE_ID, (unsigned)err);
        return -1;
    }

    log_line("tile: installed/updated %s -> http://127.0.0.1:18195/",
             AUDIOBRIDGE_TITLE_ID);
    notify("AudioBridge: dashboard tile is ready");
    return 1;
}

int tile_uninstall(void)
{
    char marker[160];
    int err;

    if (!marker_read(marker, sizeof marker)) {
        log_line("tile uninstall: ownership marker missing; refusing cleanup");
        notify("AudioBridge: tile not removed (ownership check failed)");
        return -1;
    }
    if ((err = sceAppInstUtilInitialize()) != 0) {
        log_line("tile uninstall: sceAppInstUtilInitialize -> %#x", (unsigned)err);
        return -1;
    }
    if ((err = sceAppInstUtilAppUnInstall(AUDIOBRIDGE_TITLE_ID)) != 0) {
        log_line("tile uninstall: AppUnInstall -> %#x", (unsigned)err);
        return -1;
    }

    /* AppUnInstall normally removes these. Exact-path cleanup handles firmware
     * variants that leave the source directory behind. No recursive deletion. */
    unlink(TILE_PARAM);
    unlink(TILE_ICON);
    unlink(TILE_MARKER);
    rmdir(TILE_SCE_SYS);
    rmdir(TILE_ROOT);
    log_line("tile uninstall: removed %s; pairing data retained",
             AUDIOBRIDGE_TITLE_ID);
    notify("AudioBridge: dashboard tile removed; pairing data retained");
    return 0;
}
