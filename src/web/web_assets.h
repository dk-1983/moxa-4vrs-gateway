#ifndef FOURVRS_WEB_ASSETS_H
#define FOURVRS_WEB_ASSETS_H
#include <stddef.h>
typedef struct web_asset {const char *path,*type;const unsigned char *bytes;size_t size;const char *etag;} web_asset_t;
extern const char web_inline_csp[];
extern const web_asset_t web_assets[];
#endif
