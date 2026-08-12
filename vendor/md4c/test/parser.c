#include "md4c.h"

#include <stddef.h>
#include <string.h>

#if defined MD4C_USE_UTF16
    #define T(x) L##x
#else
    #define T(x) x
#endif

typedef struct EXPECTED_tag {
    const MD_CHAR* input;
    MD_SPANTYPE type;
    MD_OFFSET begin;
    MD_OFFSET end;
    MD_OFFSET content_begin;
    MD_OFFSET content_end;
} EXPECTED;

typedef struct RECORDER_tag {
    const EXPECTED* expected;
    int blocks;
    int spans;
    int saw_span;
    int saw_content;
    int failed;
} RECORDER;

static int
enter_block(MD_BLOCKTYPE type, void* detail,
            const MD_SOURCE_RANGE* source, void* userdata)
{
    RECORDER* recorder = (RECORDER*)userdata;
    (void)type;
    (void)detail;
    (void)source;
    recorder->blocks++;
    return 0;
}

static int
leave_block(MD_BLOCKTYPE type, void* detail,
            const MD_SOURCE_RANGE* source, void* userdata)
{
    RECORDER* recorder = (RECORDER*)userdata;
    (void)type;
    (void)detail;
    (void)source;
    recorder->blocks--;
    if(recorder->blocks < 0) recorder->failed = 1;
    return 0;
}

static int
enter_span(MD_SPANTYPE type, void* detail,
           const MD_SOURCE_RANGE* source, void* userdata)
{
    RECORDER* recorder = (RECORDER*)userdata;
    (void)detail;
    recorder->spans++;
    if(type == recorder->expected->type &&
       source->begin == recorder->expected->begin &&
       source->end == recorder->expected->end)
        recorder->saw_span = 1;
    return 0;
}

static int
leave_span(MD_SPANTYPE type, void* detail,
           const MD_SOURCE_RANGE* source, void* userdata)
{
    RECORDER* recorder = (RECORDER*)userdata;
    (void)type;
    (void)detail;
    (void)source;
    recorder->spans--;
    if(recorder->spans < 0) recorder->failed = 1;
    return 0;
}

static int
text_callback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size,
              const MD_SOURCE_RANGE* source, void* userdata)
{
    RECORDER* recorder = (RECORDER*)userdata;
    (void)type;
    (void)text;
    (void)size;
    if(source->begin == recorder->expected->content_begin &&
       source->end == recorder->expected->content_end)
        recorder->saw_content = 1;
    return 0;
}

static int
run_case(const EXPECTED* expected)
{
    MD_PARSER parser;
    RECORDER recorder;
    MD_SIZE size = 0;
    memset(&parser, 0, sizeof(parser));
    memset(&recorder, 0, sizeof(recorder));
    while(expected->input[size] != 0) size++;
    recorder.expected = expected;
    parser.abi_version = MD_PARSER_ABI_VERSION;
    parser.flags = MD_FLAG_HIGHLIGHT | MD_FLAG_STRIKETHROUGH |
                   MD_FLAG_SUPERSCRIPTS | MD_FLAG_SUBSCRIPTS |
                   MD_FLAG_TINTA_HTML;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = text_callback;
    if(md_parse(expected->input, size, &parser, &recorder) != 0)
        return 0;
    return !recorder.failed && recorder.blocks == 0 && recorder.spans == 0 &&
           recorder.saw_span && recorder.saw_content;
}

int
main(void)
{
    static const EXPECTED cases[] = {
        { T("a ==m== b"), MD_SPAN_MARK, 2, 7, 4, 5 },
        { T("~~s~~"), MD_SPAN_DEL, 0, 5, 2, 3 },
        { T("^2^"), MD_SPAN_SUPERSCRIPT, 0, 3, 1, 2 },
        { T("~3~"), MD_SPAN_SUBSCRIPT, 0, 3, 1, 2 },
        { T("<p>==h==</p>"), MD_SPAN_MARK, 3, 8, 5, 6 }
    };
    size_t i;
    for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if(!run_case(&cases[i]))
            return 1;
    }
    return 0;
}
