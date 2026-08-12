#include "md4c-test.h"

#include <stdint.h>
#include <stdlib.h>

static _Thread_local size_t md4c_test_fail_at = SIZE_MAX;
static _Thread_local size_t md4c_test_calls;
static _Thread_local size_t md4c_test_live;

void
md4c_test_allocator_reset(void)
{
    md4c_test_fail_at = SIZE_MAX;
    md4c_test_calls = 0;
    md4c_test_live = 0;
}

void
md4c_test_allocator_fail_at(size_t call_index)
{
    md4c_test_fail_at = call_index;
    md4c_test_calls = 0;
}

size_t
md4c_test_allocator_call_count(void)
{
    return md4c_test_calls;
}

size_t
md4c_test_allocator_live_count(void)
{
    return md4c_test_live;
}

static int
md4c_test_should_fail(void)
{
    size_t call_index = md4c_test_calls++;
    return call_index == md4c_test_fail_at;
}

void*
md4c_test_malloc(size_t size)
{
    void* pointer;
    if(md4c_test_should_fail())
        return NULL;
    pointer = malloc(size);
    if(pointer != NULL)
        md4c_test_live++;
    return pointer;
}

void*
md4c_test_realloc(void* pointer, size_t size)
{
    void* resized;
    if(md4c_test_should_fail())
        return NULL;
    resized = realloc(pointer, size);
    if(resized != NULL && pointer == NULL)
        md4c_test_live++;
    return resized;
}

void
md4c_test_free(void* pointer)
{
    if(pointer != NULL) {
        if(md4c_test_live > 0)
            md4c_test_live--;
        free(pointer);
    }
}
