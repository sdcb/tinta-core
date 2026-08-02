#ifndef TINTA_IMAGE_CACHE_H
#define TINTA_IMAGE_CACHE_H

#include <stdbool.h>
#include <windows.h>

bool tinta_image_cache_get(const wchar_t *uri, UINT *width, UINT *height,
                           UINT *stride, UINT *buffer_size, BYTE **pixels);
void tinta_image_cache_put(const wchar_t *uri, UINT width, UINT height,
                           UINT stride, UINT buffer_size, const BYTE *pixels);
void tinta_image_cache_uninitialize(void);

#endif
