#ifndef MD4C_TEST_H
#define MD4C_TEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test-only allocation fault injection. This header is not installed and the
 * functions are linked only into test-support builds. */
void md4c_test_allocator_reset(void);
void md4c_test_allocator_fail_at(size_t call_index);
size_t md4c_test_allocator_call_count(void);
size_t md4c_test_allocator_live_count(void);

void* md4c_test_malloc(size_t size);
void* md4c_test_realloc(void* pointer, size_t size);
void md4c_test_free(void* pointer);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* MD4C_TEST_H */
