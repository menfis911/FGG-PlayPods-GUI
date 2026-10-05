#ifndef FGG_WEB_ASSETS_H
#define FGG_WEB_ASSETS_H

#include <stddef.h>

typedef struct {
    const char *data;
    size_t size;
    const char *content_type;
} web_asset;

int web_asset_find(const char *path, web_asset *asset);

#endif
