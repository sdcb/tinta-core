#include "test_harness.h"
#include "tinta_core.h"

#include <cstring>
#include <cwchar>
#include <limits>
#include <string>

void test_core_vector(TintaTestContext &tests) {
    TintaVec values{};
    const bool initialized = tinta_vec_init(&values, sizeof(int));
    tests.check(initialized, "vector initialization");
    if (!initialized) return;

    bool pushed = true;
    for (int value = 0; value < 1000; value++) {
        if (!tinta_vec_push(&values, &value)) {
            pushed = false;
            break;
        }
    }
    tests.check(pushed, "vector growth");
    tests.check(values.len == 1000 && TINTA_VEC_AT(int, values, 999) == 999,
                "vector values");
    tests.check(!tinta_vec_reserve(&values,
                                   std::numeric_limits<size_t>::max()),
                "vector overflow guard");
    tinta_vec_destroy(&values);
}

void test_core_string8(TintaTestContext &tests) {
    TintaStr8 text{};
    const char seed[] = "123456789abcdef";
    const bool assigned = tinta_str8_assign(&text, seed, std::strlen(seed));
    tests.check(assigned, "UTF-8 string initialization");
    if (assigned) {
        tests.check(tinta_str8_append(&text, text.data, text.len),
                    "UTF-8 self append");
        tests.check(std::strcmp(text.data,
                                "123456789abcdef123456789abcdef") == 0,
                    "UTF-8 self append value");
        tests.check(!tinta_str8_reserve(&text,
                                        std::numeric_limits<size_t>::max()),
                    "UTF-8 overflow guard");
    }
    tests.check(!tinta_str8_dup("x", std::numeric_limits<size_t>::max()),
                "UTF-8 duplicate overflow guard");
    tinta_str8_destroy(&text);
}

void test_core_string16(TintaTestContext &tests) {
    TintaStr16 text{};
    const wchar_t seed[] = L"123456789abcdef";
    const bool assigned = tinta_str16_assign(&text, seed, 15);
    tests.check(assigned, "UTF-16 string initialization");
    if (assigned) {
        tests.check(tinta_str16_append(&text, text.data, text.len),
                    "UTF-16 self append");
        tests.check(tinta_str16_insert(&text, 15, text.data, 3),
                    "UTF-16 aliased insertion");
        tests.check(std::wcscmp(text.data,
                                L"123456789abcdef123123456789abcdef") == 0,
                    "UTF-16 aliased insertion value");
        tests.check(tinta_str16_erase(&text, 15, 3), "UTF-16 erase");
        tests.check(std::wcscmp(text.data,
                                L"123456789abcdef123456789abcdef") == 0,
                    "UTF-16 erase value");
        tests.check(!tinta_str16_insert(&text, text.len + 1, L"x", 1),
                    "UTF-16 out-of-range insertion");
        tests.check(!tinta_str16_erase(&text, text.len, 1),
                    "UTF-16 out-of-range erase");
        tests.check(!tinta_str16_reserve(&text,
                                         std::numeric_limits<size_t>::max()),
                    "UTF-16 overflow guard");
    }
    tinta_str16_destroy(&text);

    TintaStr16 overlapping{};
    const bool overlap_assigned = tinta_str16_assign(&overlapping, L"abcdef", 6);
    tests.check(overlap_assigned, "UTF-16 overlap initialization");
    if (overlap_assigned) {
        tests.check(tinta_str16_insert(&overlapping, 2, overlapping.data + 1, 4),
                    "UTF-16 overlapping insertion");
        tests.check(std::wcscmp(overlapping.data, L"abbcdecdef") == 0,
                    "UTF-16 overlapping insertion value");
    }
    tinta_str16_destroy(&overlapping);
}

void test_core_newlines(TintaTestContext &tests) {
    TintaStr16 text{};
    const bool assigned = tinta_str16_assign(&text, L"a\r\nb\rc\n", 7);
    tests.check(assigned, "UTF-16 newline initialization");
    if (assigned) {
        tinta_str16_normalize_newlines(&text);
        tests.check(std::wcscmp(text.data, L"a\nb\nc\n") == 0,
                    "UTF-16 newline normalization");
    }
    tinta_str16_destroy(&text);
}

void test_core_map(TintaTestContext &tests) {
    TintaStringMap map{};
    const bool initialized = tinta_map_init(&map);
    tests.check(initialized, "map initialization");
    if (!initialized) return;

    tests.check(tinta_map_set(&map, nullptr, 0, 77), "empty map key set");
    size_t value = 0;
    tests.check(tinta_map_get(&map, nullptr, 0, &value) && value == 77,
                "empty map key lookup");

    bool inserted = true;
    for (size_t index = 0; index < 2000; index++) {
        const std::string key = "key-" + std::to_string(index);
        if (!tinta_map_set(&map, key.data(), key.size(), index)) {
            inserted = false;
            break;
        }
    }
    tests.check(inserted, "map growth");

    bool values_match = true;
    for (size_t index = 0; index < 2000; index++) {
        const std::string key = "key-" + std::to_string(index);
        if (!tinta_map_get(&map, key.data(), key.size(), &value) ||
            value != index) {
            values_match = false;
            break;
        }
    }
    tests.check(values_match, "map lookup after growth");
    tinta_map_destroy(&map);
}
