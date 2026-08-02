#ifndef TINTA_CORE_H
#define TINTA_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TintaVec {
    void *data;
    size_t len;
    size_t cap;
    size_t elem_size;
} TintaVec;

bool tinta_vec_init(TintaVec *vec, size_t elem_size);
void tinta_vec_destroy(TintaVec *vec);
void tinta_vec_clear(TintaVec *vec);
bool tinta_vec_reserve(TintaVec *vec, size_t capacity);
void *tinta_vec_push(TintaVec *vec, const void *item);
void *tinta_vec_push_zero(TintaVec *vec);
bool tinta_vec_resize(TintaVec *vec, size_t length);

#define TINTA_VEC_AT(type, vec, index) (((type *)(vec).data)[(index)])
#define TINTA_VEC_PTR(type, vec, index) (&((type *)(vec).data)[(index)])

typedef struct TintaStr8 {
    char *data;
    size_t len;
    size_t cap;
} TintaStr8;

typedef struct TintaStr16 {
    wchar_t *data;
    size_t len;
    size_t cap;
} TintaStr16;

bool tinta_str8_init(TintaStr8 *str);
void tinta_str8_destroy(TintaStr8 *str);
void tinta_str8_clear(TintaStr8 *str);
bool tinta_str8_reserve(TintaStr8 *str, size_t capacity);
bool tinta_str8_assign(TintaStr8 *str, const char *data, size_t length);
bool tinta_str8_append(TintaStr8 *str, const char *data, size_t length);
bool tinta_str8_append_char(TintaStr8 *str, char value);
char *tinta_str8_dup(const char *data, size_t length);

bool tinta_str16_init(TintaStr16 *str);
void tinta_str16_destroy(TintaStr16 *str);
void tinta_str16_clear(TintaStr16 *str);
bool tinta_str16_reserve(TintaStr16 *str, size_t capacity);
bool tinta_str16_assign(TintaStr16 *str, const wchar_t *data, size_t length);
bool tinta_str16_append(TintaStr16 *str, const wchar_t *data, size_t length);
bool tinta_str16_insert(TintaStr16 *str, size_t position,
                        const wchar_t *data, size_t length);
bool tinta_str16_erase(TintaStr16 *str, size_t position, size_t length);
void tinta_str16_normalize_newlines(TintaStr16 *str);

typedef struct TintaStringMapEntry {
    uint64_t hash;
    char *key;
    size_t key_len;
    size_t value;
    bool occupied;
} TintaStringMapEntry;

typedef struct TintaStringMap {
    TintaStringMapEntry *entries;
    size_t len;
    size_t cap;
} TintaStringMap;

bool tinta_map_init(TintaStringMap *map);
void tinta_map_destroy(TintaStringMap *map);
void tinta_map_clear(TintaStringMap *map);
bool tinta_map_get(const TintaStringMap *map, const char *key, size_t key_len,
                   size_t *value);
bool tinta_map_set(TintaStringMap *map, const char *key, size_t key_len,
                   size_t value);

uint64_t tinta_hash_bytes(const void *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
