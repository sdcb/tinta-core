#include "tinta_math.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    TintaMathParseResult result = tinta_math_parse(
        (const char *)data, size, 4096, 128);
    tinta_math_node_destroy(result.root);
    return 0;
}

#ifndef TINTA_LIBFUZZER
int main(int argc, char **argv) {
    FILE *file = stdin;
    uint8_t *data;
    long size;
    if (argc > 1) file = fopen(argv[1], "rb");
    if (!file) return 1;
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET)) {
        if (file != stdin) fclose(file);
        return 1;
    }
    data = (uint8_t *)malloc((size_t)size);
    if (size && !data) {
        if (file != stdin) fclose(file);
        return 1;
    }
    if (size && fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        if (file != stdin) fclose(file);
        return 1;
    }
    if (file != stdin) fclose(file);
    LLVMFuzzerTestOneInput(data, (size_t)size);
    free(data);
    return 0;
}
#endif
