#include "document.h"

#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    TintaMarkdownOptions options = tinta_markdown_default_options();
    TintaParseResult result;
    options.max_nodes = 100000;
    options.max_depth = 256;
    result = tinta_parse_document((const char *)data, size, "fuzz.md", &options);
    tinta_parse_result_destroy(&result);
    return 0;
}

#ifndef TINTA_LIBFUZZER
int main(int argc, char **argv) {
    FILE *file = stdin;
    unsigned char *data;
    long size;
    if (argc > 1) file = fopen(argv[1], "rb");
    if (!file) return 1;
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET)) {
        if (file != stdin) fclose(file);
        return 1;
    }
    data = (unsigned char *)malloc((size_t)size);
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
