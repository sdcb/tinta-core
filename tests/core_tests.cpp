#include "tinta_core.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <limits>
#include <string>

int main() {
    TintaVec values{};
    if (!tinta_vec_init(&values, sizeof(int))) return 1;
    for (int value = 0; value < 1000; value++) {
        if (!tinta_vec_push(&values, &value)) return 1;
    }
    if (values.len != 1000 || TINTA_VEC_AT(int, values, 999) != 999 ||
        tinta_vec_reserve(&values, std::numeric_limits<size_t>::max())) {
        std::cerr << "vector growth or overflow guard failed\n";
        return 1;
    }
    tinta_vec_destroy(&values);

    TintaStr8 text8{};
    const char seed8[] = "123456789abcdef";
    if (!tinta_str8_assign(&text8, seed8, std::strlen(seed8)) ||
        !tinta_str8_append(&text8, text8.data, text8.len) ||
        std::strcmp(text8.data, "123456789abcdef123456789abcdef") != 0 ||
        tinta_str8_reserve(&text8, std::numeric_limits<size_t>::max()) ||
        tinta_str8_dup("x", std::numeric_limits<size_t>::max())) {
        std::cerr << "UTF-8 string aliasing or overflow guard failed\n";
        return 1;
    }
    tinta_str8_destroy(&text8);

    TintaStr16 text16{};
    const wchar_t seed16[] = L"123456789abcdef";
    if (!tinta_str16_assign(&text16, seed16, 15) ||
        !tinta_str16_append(&text16, text16.data, text16.len) ||
        !tinta_str16_insert(&text16, 15, text16.data, 3) ||
        std::wcscmp(text16.data, L"123456789abcdef123123456789abcdef") != 0 ||
        !tinta_str16_erase(&text16, 15, 3) ||
        std::wcscmp(text16.data, L"123456789abcdef123456789abcdef") != 0 ||
        tinta_str16_insert(&text16, text16.len + 1, L"x", 1) ||
        tinta_str16_erase(&text16, text16.len, 1) ||
        tinta_str16_reserve(&text16, std::numeric_limits<size_t>::max())) {
        std::cerr << "UTF-16 string aliasing or overflow guard failed\n";
        return 1;
    }
    tinta_str16_destroy(&text16);

    TintaStr16 overlapping16{};
    if (!tinta_str16_assign(&overlapping16, L"abcdef", 6) ||
        !tinta_str16_insert(&overlapping16, 2, overlapping16.data + 1, 4) ||
        std::wcscmp(overlapping16.data, L"abbcdecdef") != 0) {
        std::cerr << "UTF-16 overlapping insertion failed\n";
        return 1;
    }
    tinta_str16_destroy(&overlapping16);

    TintaStr16 newlines16{};
    if (!tinta_str16_assign(&newlines16, L"a\r\nb\rc\n", 7)) return 1;
    tinta_str16_normalize_newlines(&newlines16);
    if (std::wcscmp(newlines16.data, L"a\nb\nc\n") != 0) {
        std::cerr << "UTF-16 newline normalization failed\n";
        return 1;
    }
    tinta_str16_destroy(&newlines16);

    TintaStringMap map{};
    if (!tinta_map_init(&map) || !tinta_map_set(&map, nullptr, 0, 77)) return 1;
    size_t value = 0;
    if (!tinta_map_get(&map, nullptr, 0, &value) || value != 77) {
        std::cerr << "empty map key failed\n";
        return 1;
    }
    for (size_t index = 0; index < 2000; index++) {
        std::string key = "key-" + std::to_string(index);
        if (!tinta_map_set(&map, key.data(), key.size(), index)) return 1;
    }
    for (size_t index = 0; index < 2000; index++) {
        std::string key = "key-" + std::to_string(index);
        if (!tinta_map_get(&map, key.data(), key.size(), &value) || value != index) {
            std::cerr << "map lookup failed at " << index << "\n";
            return 1;
        }
    }
    tinta_map_destroy(&map);
    return 0;
}
