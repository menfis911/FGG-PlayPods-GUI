#ifndef TEST_WEB_ASSETS_H
#define TEST_WEB_ASSETS_H
#include <stddef.h>
typedef struct {
    const char *content_type;
    const unsigned char *data;
    size_t size;
} web_asset;
int web_asset_find(const char *path, web_asset *asset);
#endif
