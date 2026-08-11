#include "tinta_math.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    TintaMathParseResult result = tinta_math_parse(
        (const char *)data, size, 4096, 128);
    tinta_math_node_destroy(result.root);
    return 0;
}
