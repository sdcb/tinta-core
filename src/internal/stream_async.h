#ifndef TINTA_STREAM_ASYNC_H
#define TINTA_STREAM_ASYNC_H

#include "app.h"

typedef struct TintaStreamAsync TintaStreamAsync;

typedef struct TintaStreamParseResult {
    uint64_t revision;
    size_t utf8_length;
    bool success;
    TintaPreparedSource prepared;
} TintaStreamParseResult;

TintaStreamAsync *tinta_stream_async_create(HWND hwnd);
void tinta_stream_async_close(TintaStreamAsync *async);
bool tinta_stream_async_submit(TintaStreamAsync *async, uint64_t revision,
                               const char *source, size_t length,
                               const wchar_t *path, size_t max_nodes,
                               size_t max_depth);
TintaStreamParseResult *tinta_stream_async_take(TintaStreamAsync *async);
void tinta_stream_parse_result_destroy(TintaStreamParseResult *result);

#endif
