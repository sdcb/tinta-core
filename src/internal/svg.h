#ifndef TINTA_SVG_H
#define TINTA_SVG_H

#include "app.h"

typedef struct TintaSvgInfo {
    float width;
    float height;
} TintaSvgInfo;

#ifdef __cplusplus
extern "C" {
#endif

bool tinta_svg_uri_candidate(const char *uri);
bool tinta_svg_data_uri(const char *uri);
bool tinta_svg_decode_data_uri(const char *uri, size_t maximum_bytes,
                               TintaStr8 *source, TintaSvgInfo *info);
bool tinta_svg_prepare_source(const char *data, size_t length,
                              size_t maximum_bytes, TintaSvgInfo *info);

#ifdef __cplusplus
}
#endif

#endif
