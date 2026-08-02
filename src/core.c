#include "tinta_core.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool tinta_size_mul(size_t a, size_t b, size_t *result) {
    if (a != 0 && b > SIZE_MAX / a) return false;
    *result = a * b;
    return true;
}

bool tinta_vec_init(TintaVec *vec, size_t elem_size) {
    if (!vec || elem_size == 0) return false;
    memset(vec, 0, sizeof(*vec));
    vec->elem_size = elem_size;
    return true;
}

void tinta_vec_destroy(TintaVec *vec) {
    if (!vec) return;
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

void tinta_vec_clear(TintaVec *vec) {
    if (vec) vec->len = 0;
}

bool tinta_vec_reserve(TintaVec *vec, size_t capacity) {
    size_t bytes;
    void *data;
    size_t grown;
    if (!vec || vec->elem_size == 0) return false;
    if (capacity <= vec->cap) return true;
    grown = vec->cap ? vec->cap : 8;
    while (grown < capacity) {
        size_t next = grown + grown / 2 + 8;
        if (next <= grown) {
            grown = capacity;
            break;
        }
        grown = next;
    }
    if (!tinta_size_mul(grown, vec->elem_size, &bytes)) return false;
    data = realloc(vec->data, bytes);
    if (!data) return false;
    vec->data = data;
    vec->cap = grown;
    return true;
}

void *tinta_vec_push(TintaVec *vec, const void *item) {
    unsigned char *slot;
    if (!vec || !item || vec->len == SIZE_MAX ||
        !tinta_vec_reserve(vec, vec->len + 1)) return NULL;
    slot = (unsigned char *)vec->data + vec->len * vec->elem_size;
    memcpy(slot, item, vec->elem_size);
    vec->len++;
    return slot;
}

void *tinta_vec_push_zero(TintaVec *vec) {
    unsigned char *slot;
    if (!vec || vec->len == SIZE_MAX ||
        !tinta_vec_reserve(vec, vec->len + 1)) return NULL;
    slot = (unsigned char *)vec->data + vec->len * vec->elem_size;
    memset(slot, 0, vec->elem_size);
    vec->len++;
    return slot;
}

bool tinta_vec_resize(TintaVec *vec, size_t length) {
    size_t old_len;
    if (!vec || !tinta_vec_reserve(vec, length)) return false;
    old_len = vec->len;
    if (length > old_len) {
        memset((unsigned char *)vec->data + old_len * vec->elem_size, 0,
               (length - old_len) * vec->elem_size);
    }
    vec->len = length;
    return true;
}

bool tinta_str8_init(TintaStr8 *str) {
    if (!str) return false;
    memset(str, 0, sizeof(*str));
    return true;
}

void tinta_str8_destroy(TintaStr8 *str) {
    if (!str) return;
    free(str->data);
    memset(str, 0, sizeof(*str));
}

void tinta_str8_clear(TintaStr8 *str) {
    if (!str) return;
    str->len = 0;
    if (str->data) str->data[0] = '\0';
}

bool tinta_str8_reserve(TintaStr8 *str, size_t capacity) {
    char *data;
    size_t grown;
    if (!str || capacity == SIZE_MAX) return false;
    if (capacity + 1 <= str->cap) return true;
    grown = str->cap ? str->cap : 16;
    while (grown < capacity + 1) {
        size_t next = grown + grown / 2 + 16;
        if (next <= grown) return false;
        grown = next;
    }
    data = (char *)realloc(str->data, grown);
    if (!data) return false;
    str->data = data;
    str->cap = grown;
    if (str->len == 0) str->data[0] = '\0';
    return true;
}

bool tinta_str8_assign(TintaStr8 *str, const char *data, size_t length) {
    size_t source_offset = 0;
    bool aliases = false;
    if (str && data && str->data && (uintptr_t)data >= (uintptr_t)str->data &&
        (uintptr_t)data - (uintptr_t)str->data < str->cap) {
        aliases = true;
        source_offset = (size_t)((uintptr_t)data - (uintptr_t)str->data);
    }
    if (!str || (!data && length != 0) || !tinta_str8_reserve(str, length)) return false;
    if (!str->data) return false;
    if (aliases) data = str->data + source_offset;
    if (length) memmove(str->data, data, length);
    str->data[length] = '\0';
    str->len = length;
    return true;
}

bool tinta_str8_append(TintaStr8 *str, const char *data, size_t length) {
    size_t source_offset = 0;
    bool aliases = false;
    if (str && data && str->data && (uintptr_t)data >= (uintptr_t)str->data &&
        (uintptr_t)data - (uintptr_t)str->data < str->cap) {
        aliases = true;
        source_offset = (size_t)((uintptr_t)data - (uintptr_t)str->data);
    }
    if (!str || (!data && length != 0) || length > SIZE_MAX - str->len ||
        !tinta_str8_reserve(str, str->len + length)) return false;
    if (!str->data) return false;
    if (aliases) data = str->data + source_offset;
    if (length) memmove(str->data + str->len, data, length);
    str->len += length;
    str->data[str->len] = '\0';
    return true;
}

bool tinta_str8_append_char(TintaStr8 *str, char value) {
    return tinta_str8_append(str, &value, 1);
}

char *tinta_str8_dup(const char *data, size_t length) {
    char *copy;
    if ((!data && length != 0) || length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1);
    if (!copy) return NULL;
    if (length) memcpy(copy, data, length);
    copy[length] = '\0';
    return copy;
}

bool tinta_str16_init(TintaStr16 *str) {
    if (!str) return false;
    memset(str, 0, sizeof(*str));
    return true;
}

void tinta_str16_destroy(TintaStr16 *str) {
    if (!str) return;
    free(str->data);
    memset(str, 0, sizeof(*str));
}

void tinta_str16_clear(TintaStr16 *str) {
    if (!str) return;
    str->len = 0;
    if (str->data) str->data[0] = L'\0';
}

bool tinta_str16_reserve(TintaStr16 *str, size_t capacity) {
    wchar_t *data;
    size_t grown;
    size_t bytes;
    if (!str || capacity == SIZE_MAX) return false;
    if (capacity + 1 <= str->cap) return true;
    grown = str->cap ? str->cap : 16;
    while (grown < capacity + 1) {
        size_t next = grown + grown / 2 + 16;
        if (next <= grown) return false;
        grown = next;
    }
    if (!tinta_size_mul(grown, sizeof(wchar_t), &bytes)) return false;
    data = (wchar_t *)realloc(str->data, bytes);
    if (!data) return false;
    str->data = data;
    str->cap = grown;
    if (str->len == 0) str->data[0] = L'\0';
    return true;
}

bool tinta_str16_assign(TintaStr16 *str, const wchar_t *data, size_t length) {
    size_t source_offset = 0;
    bool aliases = false;
    if (str && data && str->data && (uintptr_t)data >= (uintptr_t)str->data &&
        (uintptr_t)data - (uintptr_t)str->data < str->cap * sizeof(wchar_t)) {
        aliases = true;
        source_offset = ((size_t)((uintptr_t)data - (uintptr_t)str->data)) /
                        sizeof(wchar_t);
    }
    if (!str || (!data && length != 0) || !tinta_str16_reserve(str, length)) return false;
    if (!str->data) return false;
    if (aliases) data = str->data + source_offset;
    if (length) memmove(str->data, data, length * sizeof(wchar_t));
    str->data[length] = L'\0';
    str->len = length;
    return true;
}

bool tinta_str16_append(TintaStr16 *str, const wchar_t *data, size_t length) {
    size_t source_offset = 0;
    bool aliases = false;
    if (str && data && str->data && (uintptr_t)data >= (uintptr_t)str->data &&
        (uintptr_t)data - (uintptr_t)str->data < str->cap * sizeof(wchar_t)) {
        aliases = true;
        source_offset = ((size_t)((uintptr_t)data - (uintptr_t)str->data)) /
                        sizeof(wchar_t);
    }
    if (!str || (!data && length != 0) || length > SIZE_MAX - str->len ||
        !tinta_str16_reserve(str, str->len + length)) return false;
    if (!str->data) return false;
    if (aliases) data = str->data + source_offset;
    if (length) memmove(str->data + str->len, data, length * sizeof(wchar_t));
    str->len += length;
    str->data[str->len] = L'\0';
    return true;
}

bool tinta_str16_insert(TintaStr16 *str, size_t position,
                        const wchar_t *data, size_t length) {
    wchar_t *copy = NULL;
    if (!str || position > str->len || (!data && length != 0) ||
        length > SIZE_MAX - str->len)
        return false;
    if (!length) return true;
    if (data && str->data && (uintptr_t)data >= (uintptr_t)str->data &&
        (uintptr_t)data - (uintptr_t)str->data < str->cap * sizeof(wchar_t)) {
        size_t bytes;
        if (!tinta_size_mul(length, sizeof(wchar_t), &bytes)) return false;
        copy = (wchar_t *)malloc(bytes ? bytes : sizeof(wchar_t));
        if (!copy) return false;
        if (length) memcpy(copy, data, bytes);
        data = copy;
    }
    if (!tinta_str16_reserve(str, str->len + length)) {
        free(copy);
        return false;
    }
    if (!str->data) {
        free(copy);
        return false;
    }
    memmove(str->data + position + length, str->data + position,
            (str->len - position + 1) * sizeof(wchar_t));
    if (length) memcpy(str->data + position, data, length * sizeof(wchar_t));
    str->len += length;
    free(copy);
    return true;
}

bool tinta_str16_erase(TintaStr16 *str, size_t position, size_t length) {
    if (!str || position > str->len || length > str->len - position)
        return false;
    if (!length) return true;
    memmove(str->data + position, str->data + position + length,
            (str->len - position - length + 1) * sizeof(wchar_t));
    str->len -= length;
    return true;
}

void tinta_str16_normalize_newlines(TintaStr16 *str) {
    size_t read;
    size_t write = 0;
    if (!str || !str->data) return;
    for (read = 0; read < str->len; read++) {
        wchar_t character = str->data[read];
        if (character == L'\r') {
            character = L'\n';
            if (read + 1 < str->len && str->data[read + 1] == L'\n') read++;
        }
        str->data[write++] = character;
    }
    str->len = write;
    str->data[write] = L'\0';
}

uint64_t tinta_hash_bytes(const void *data, size_t length) {
    const unsigned char *bytes = (const unsigned char *)data;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0; i < length; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}

bool tinta_map_init(TintaStringMap *map) {
    if (!map) return false;
    memset(map, 0, sizeof(*map));
    return true;
}

void tinta_map_clear(TintaStringMap *map) {
    size_t i;
    if (!map) return;
    for (i = 0; i < map->cap; i++) {
        if (map->entries[i].occupied) free(map->entries[i].key);
    }
    free(map->entries);
    memset(map, 0, sizeof(*map));
}

void tinta_map_destroy(TintaStringMap *map) {
    tinta_map_clear(map);
}

static bool tinta_map_insert_owned(TintaStringMapEntry *entries, size_t cap,
                                   TintaStringMapEntry entry) {
    size_t index = (size_t)(entry.hash & (uint64_t)(cap - 1));
    while (entries[index].occupied) index = (index + 1) & (cap - 1);
    entries[index] = entry;
    return true;
}

static bool tinta_map_grow(TintaStringMap *map, size_t capacity) {
    TintaStringMapEntry *entries;
    size_t cap = 16;
    size_t i;
    while (cap < capacity) {
        if (cap > SIZE_MAX / 2) return false;
        cap *= 2;
    }
    entries = (TintaStringMapEntry *)calloc(cap, sizeof(*entries));
    if (!entries) return false;
    for (i = 0; i < map->cap; i++) {
        if (map->entries[i].occupied) tinta_map_insert_owned(entries, cap, map->entries[i]);
    }
    free(map->entries);
    map->entries = entries;
    map->cap = cap;
    return true;
}

bool tinta_map_get(const TintaStringMap *map, const char *key, size_t key_len,
                   size_t *value) {
    uint64_t hash;
    size_t index;
    size_t probes;
    if (!map || !map->cap || (!key && key_len)) return false;
    hash = tinta_hash_bytes(key, key_len);
    index = (size_t)(hash & (uint64_t)(map->cap - 1));
    for (probes = 0; probes < map->cap; probes++) {
        const TintaStringMapEntry *entry = &map->entries[index];
        if (!entry->occupied) return false;
        if (entry->hash == hash && entry->key_len == key_len &&
            (!key_len || memcmp(entry->key, key, key_len) == 0)) {
            if (value) *value = entry->value;
            return true;
        }
        index = (index + 1) & (map->cap - 1);
    }
    return false;
}

bool tinta_map_set(TintaStringMap *map, const char *key, size_t key_len,
                   size_t value) {
    uint64_t hash;
    size_t index;
    char *copy;
    if (!map || (!key && key_len)) return false;
    if (!map->cap || map->len >= map->cap - map->cap / 3) {
        size_t next_capacity;
        if (map->cap > SIZE_MAX / 2) return false;
        next_capacity = map->cap ? map->cap * 2 : 16;
        if (!tinta_map_grow(map, next_capacity)) return false;
    }
    hash = tinta_hash_bytes(key, key_len);
    index = (size_t)(hash & (uint64_t)(map->cap - 1));
    while (map->entries[index].occupied) {
        TintaStringMapEntry *entry = &map->entries[index];
        if (entry->hash == hash && entry->key_len == key_len &&
            (!key_len || memcmp(entry->key, key, key_len) == 0)) {
            entry->value = value;
            return true;
        }
        index = (index + 1) & (map->cap - 1);
    }
    copy = tinta_str8_dup(key, key_len);
    if (!copy) return false;
    map->entries[index].hash = hash;
    map->entries[index].key = copy;
    map->entries[index].key_len = key_len;
    map->entries[index].value = value;
    map->entries[index].occupied = true;
    map->len++;
    return true;
}
