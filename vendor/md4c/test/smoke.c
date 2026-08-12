#include "md4c-html.h"

#include <stddef.h>

typedef struct OUTPUT_tag {
    size_t size;
    int saw_details;
} OUTPUT;

static void
process_output(const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    OUTPUT* output = (OUTPUT*)userdata;
    MD_SIZE i;
    output->size += size;
    for(i = 0; i + 7 <= size; i++) {
        if(text[i + 0] == '<' && text[i + 1] == 'd' &&
           text[i + 2] == 'e' && text[i + 3] == 't' &&
           text[i + 4] == 'a' && text[i + 5] == 'i' &&
           text[i + 6] == 'l')
            output->saw_details = 1;
    }
}

int
main(void)
{
    OUTPUT output = { 0, 0 };
#if defined MD4C_USE_UTF16
    static const MD_CHAR input[] =
        L"<details open><summary>A &amp; B</summary>\\[x+y\\]</details>";
#else
    static const MD_CHAR input[] =
        "<details open><summary>A &amp; B</summary>\\[x+y\\]</details>";
#endif
    int result = md_html(input, (MD_SIZE)(sizeof(input) / sizeof(input[0]) - 1),
                         process_output, &output,
                         MD_FLAG_LATEXMATHSPANS | MD_FLAG_TINTA_HTML, 0);
    return result == 0 && output.size > 0 && output.saw_details ? 0 : 1;
}
