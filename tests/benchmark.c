#include "markdown.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_seconds(void) {
    return (double)clock() / CLOCKS_PER_SEC;
}

int main(void) {
    static const char paragraph[] =
        "## Heading\n\nA paragraph with **bold**, [a link](https://example.com), "
        "`code`, and enough words to exercise parsing repeatedly.\n\n";
    const size_t repetitions = 10000;
    const size_t paragraph_length = sizeof(paragraph) - 1;
    const size_t length = repetitions * paragraph_length;
    TintaMarkdownOptions options = tinta_markdown_default_options();
    TintaParseResult result;
    int success;
    char *document = (char *)malloc(length + 1);
    size_t index;
    double started;
    if (!document) return 1;
    for (index = 0; index < repetitions; index++)
        memcpy(document + index * paragraph_length, paragraph,
               paragraph_length);
    document[length] = 0;
    options.max_nodes = 1000000;
    options.max_depth = 256;
    started = now_seconds();
    result = tinta_markdown_parse(document, length, &options);
    printf("markdown_bytes=%zu elapsed_ms=%.3f success=%d\n", length,
           (now_seconds() - started) * 1000.0, result.success ? 1 : 0);
    success = result.success ? 1 : 0;
    tinta_parse_result_destroy(&result);
    free(document);
    return success ? 0 : 1;
}
