#include "stream_async.h"

#include <stdlib.h>
#include <string.h>

typedef struct TintaStreamTask {
    uint64_t revision;
    size_t length;
    size_t max_nodes;
    size_t max_depth;
    char *source;
    wchar_t *path;
} TintaStreamTask;

struct TintaStreamAsync {
    LONG references;
    LONG closing;
    bool worker_active;
    SRWLOCK lock;
    HWND hwnd;
    TintaStreamTask *pending;
    TintaStreamParseResult *result;
};

static void task_destroy(TintaStreamTask *task) {
    if (!task) return;
    free(task->source);
    free(task->path);
    free(task);
}

void tinta_stream_parse_result_destroy(TintaStreamParseResult *result) {
    if (!result) return;
    tinta_app_destroy_prepared_source(&result->prepared);
    free(result);
}

static void async_release(TintaStreamAsync *async) {
    if (async && !InterlockedDecrement(&async->references)) {
        task_destroy(async->pending);
        tinta_stream_parse_result_destroy(async->result);
        free(async);
    }
}

static DWORD CALLBACK stream_worker(void *parameter) {
    TintaStreamAsync *async = (TintaStreamAsync *)parameter;
    for (;;) {
        TintaStreamTask *task;
        TintaStreamParseResult *result;
        HWND hwnd = NULL;
        AcquireSRWLockExclusive(&async->lock);
        if (async->closing || !async->pending) {
            async->worker_active = false;
            ReleaseSRWLockExclusive(&async->lock);
            break;
        }
        task = async->pending;
        async->pending = NULL;
        ReleaseSRWLockExclusive(&async->lock);

        result = (TintaStreamParseResult *)calloc(1, sizeof(*result));
        if (result) {
            result->revision = task->revision;
            result->utf8_length = task->length;
            result->success = tinta_app_prepare_source(
                task->source, task->length, task->path,
                task->max_nodes, task->max_depth, &result->prepared);
        }
        task_destroy(task);

        AcquireSRWLockExclusive(&async->lock);
        if (!async->closing && result) {
            tinta_stream_parse_result_destroy(async->result);
            async->result = result;
            result = NULL;
            hwnd = async->hwnd;
        }
        ReleaseSRWLockExclusive(&async->lock);
        tinta_stream_parse_result_destroy(result);
        if (hwnd) PostMessageW(hwnd, TINTA_WM_STREAM_PARSED, 0, 0);
    }
    async_release(async);
    return 0;
}

TintaStreamAsync *tinta_stream_async_create(HWND hwnd) {
    TintaStreamAsync *async =
        (TintaStreamAsync *)calloc(1, sizeof(*async));
    if (!async) return NULL;
    async->references = 1;
    InitializeSRWLock(&async->lock);
    async->hwnd = hwnd;
    return async;
}

void tinta_stream_async_close(TintaStreamAsync *async) {
    TintaStreamTask *pending;
    TintaStreamParseResult *result;
    if (!async) return;
    AcquireSRWLockExclusive(&async->lock);
    async->closing = 1;
    async->hwnd = NULL;
    pending = async->pending;
    result = async->result;
    async->pending = NULL;
    async->result = NULL;
    ReleaseSRWLockExclusive(&async->lock);
    task_destroy(pending);
    tinta_stream_parse_result_destroy(result);
    async_release(async);
}

bool tinta_stream_async_submit(TintaStreamAsync *async, uint64_t revision,
                               const char *source, size_t length,
                               const wchar_t *path, size_t max_nodes,
                               size_t max_depth) {
    TintaStreamTask *task;
    TintaStreamTask *replaced;
    bool start_worker = false;
    if (!async || (!source && length)) return false;
    task = (TintaStreamTask *)calloc(1, sizeof(*task));
    if (!task) return false;
    task->source = tinta_str8_dup(source ? source : "", length);
    task->path = path ? tinta_wcsdup_n(path, wcslen(path)) : NULL;
    task->revision = revision;
    task->length = length;
    task->max_nodes = max_nodes;
    task->max_depth = max_depth;
    if (!task->source || (path && !task->path)) {
        task_destroy(task);
        return false;
    }
    AcquireSRWLockExclusive(&async->lock);
    if (async->closing) {
        ReleaseSRWLockExclusive(&async->lock);
        task_destroy(task);
        return false;
    }
    replaced = async->pending;
    async->pending = task;
    if (!async->worker_active) {
        async->worker_active = true;
        InterlockedIncrement(&async->references);
        start_worker = true;
    }
    ReleaseSRWLockExclusive(&async->lock);
    task_destroy(replaced);
    if (!start_worker) return true;
    if (QueueUserWorkItem(stream_worker, async, WT_EXECUTEDEFAULT)) {
        return true;
    }
    AcquireSRWLockExclusive(&async->lock);
    async->worker_active = false;
    ReleaseSRWLockExclusive(&async->lock);
    async_release(async);
    return false;
}

TintaStreamParseResult *tinta_stream_async_take(TintaStreamAsync *async) {
    TintaStreamParseResult *result;
    if (!async) return NULL;
    AcquireSRWLockExclusive(&async->lock);
    result = async->result;
    async->result = NULL;
    ReleaseSRWLockExclusive(&async->lock);
    return result;
}
