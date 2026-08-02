#include "image_cache.h"

#include <stdlib.h>
#include <string.h>

typedef struct TintaImageCacheEntry {
    struct TintaImageCacheEntry *next;
    wchar_t *uri;
    BYTE *pixels;
    UINT width;
    UINT height;
    UINT stride;
    UINT buffer_size;
    ULONGLONG used;
} TintaImageCacheEntry;

static SRWLOCK g_cache_lock = SRWLOCK_INIT;
static TintaImageCacheEntry *g_cache;
static size_t g_cache_bytes;
static size_t g_cache_count;

static void cache_entry_destroy(TintaImageCacheEntry *entry) {
    if (!entry) return;
    free(entry->uri);
    free(entry->pixels);
    free(entry);
}

static void cache_trim(void) {
    const size_t max_bytes = 128u * 1024u * 1024u;
    const size_t max_entries = 128u;
    while (g_cache &&
           (g_cache_bytes > max_bytes || g_cache_count > max_entries)) {
        TintaImageCacheEntry **oldest_link = &g_cache;
        TintaImageCacheEntry **link = &g_cache;
        ULONGLONG oldest = ~(ULONGLONG)0;
        while (*link) {
            if ((*link)->used < oldest) {
                oldest = (*link)->used;
                oldest_link = link;
            }
            link = &(*link)->next;
        }
        if (*oldest_link) {
            TintaImageCacheEntry *entry = *oldest_link;
            *oldest_link = entry->next;
            g_cache_bytes -= entry->buffer_size;
            g_cache_count--;
            cache_entry_destroy(entry);
        }
    }
}

bool tinta_image_cache_get(const wchar_t *uri, UINT *width, UINT *height,
                           UINT *stride, UINT *buffer_size, BYTE **pixels) {
    TintaImageCacheEntry *entry;
    BYTE *copy = NULL;
    if (!uri || !width || !height || !stride || !buffer_size || !pixels)
        return false;
    *pixels = NULL;
    AcquireSRWLockExclusive(&g_cache_lock);
    for (entry = g_cache; entry; entry = entry->next) {
        if (!_wcsicmp(entry->uri, uri)) {
            copy = (BYTE *)malloc(entry->buffer_size);
            if (copy) {
                memcpy(copy, entry->pixels, entry->buffer_size);
                *width = entry->width;
                *height = entry->height;
                *stride = entry->stride;
                *buffer_size = entry->buffer_size;
                entry->used = GetTickCount64();
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_cache_lock);
    *pixels = copy;
    return copy != NULL;
}

void tinta_image_cache_put(const wchar_t *uri, UINT width, UINT height,
                           UINT stride, UINT buffer_size, const BYTE *pixels) {
    TintaImageCacheEntry *entry;
    size_t uri_length;
    if (!uri || !width || !height || !stride || !buffer_size || !pixels ||
        buffer_size > 128u * 1024u * 1024u)
        return;
    entry = (TintaImageCacheEntry *)calloc(1, sizeof(*entry));
    if (!entry) return;
    uri_length = wcslen(uri);
    entry->uri = (wchar_t *)malloc((uri_length + 1) * sizeof(wchar_t));
    entry->pixels = (BYTE *)malloc(buffer_size);
    if (!entry->uri || !entry->pixels) {
        cache_entry_destroy(entry);
        return;
    }
    memcpy(entry->uri, uri, (uri_length + 1) * sizeof(wchar_t));
    memcpy(entry->pixels, pixels, buffer_size);
    entry->width = width;
    entry->height = height;
    entry->stride = stride;
    entry->buffer_size = buffer_size;
    entry->used = GetTickCount64();
    AcquireSRWLockExclusive(&g_cache_lock);
    {
        TintaImageCacheEntry **link = &g_cache;
        while (*link) {
            if (!_wcsicmp((*link)->uri, uri)) {
                TintaImageCacheEntry *replaced = *link;
                *link = replaced->next;
                g_cache_bytes -= replaced->buffer_size;
                g_cache_count--;
                cache_entry_destroy(replaced);
                break;
            }
            link = &(*link)->next;
        }
    }
    entry->next = g_cache;
    g_cache = entry;
    g_cache_bytes += buffer_size;
    g_cache_count++;
    cache_trim();
    ReleaseSRWLockExclusive(&g_cache_lock);
}

void tinta_image_cache_uninitialize(void) {
    TintaImageCacheEntry *entry;
    AcquireSRWLockExclusive(&g_cache_lock);
    entry = g_cache;
    g_cache = NULL;
    g_cache_bytes = 0;
    g_cache_count = 0;
    ReleaseSRWLockExclusive(&g_cache_lock);
    while (entry) {
        TintaImageCacheEntry *next = entry->next;
        cache_entry_destroy(entry);
        entry = next;
    }
}
