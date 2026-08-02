#ifndef TINTA_MARKDOWN_H
#define TINTA_MARKDOWN_H

#include "tinta_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TintaElementType {
    TINTA_ELEMENT_DOCUMENT,
    TINTA_ELEMENT_PARAGRAPH,
    TINTA_ELEMENT_HEADING,
    TINTA_ELEMENT_CODE_BLOCK,
    TINTA_ELEMENT_MERMAID_DIAGRAM,
    TINTA_ELEMENT_BLOCK_QUOTE,
    TINTA_ELEMENT_LIST,
    TINTA_ELEMENT_LIST_ITEM,
    TINTA_ELEMENT_HORIZONTAL_RULE,
    TINTA_ELEMENT_TABLE,
    TINTA_ELEMENT_TABLE_ROW,
    TINTA_ELEMENT_TABLE_CELL,
    TINTA_ELEMENT_HTML_BLOCK,
    TINTA_ELEMENT_TEXT,
    TINTA_ELEMENT_CODE,
    TINTA_ELEMENT_EMPHASIS,
    TINTA_ELEMENT_STRONG,
    TINTA_ELEMENT_LINK,
    TINTA_ELEMENT_IMAGE,
    TINTA_ELEMENT_SOFT_BREAK,
    TINTA_ELEMENT_HARD_BREAK,
    TINTA_ELEMENT_RUBY,
    TINTA_ELEMENT_RUBY_TEXT,
    TINTA_ELEMENT_HIGHLIGHT,
    TINTA_ELEMENT_SUPERSCRIPT,
    TINTA_ELEMENT_SUBSCRIPT,
    TINTA_ELEMENT_STRIKETHROUGH
} TintaElementType;

typedef struct TintaElement TintaElement;

struct TintaElement {
    TintaElementType type;
    char *text;
    char *url;
    char *title;
    char *language;
    int level;
    bool ordered;
    bool task;
    bool task_checked;
    int start;
    int align;
    int col_count;
    int alert_kind;
    size_t source_offset;
    TintaElement **children;
    size_t child_count;
    size_t child_capacity;
    TintaElement *parent;
};

typedef struct TintaMarkdownOptions {
    int tab_width;
    bool permissive_auto_links;
    bool permissive_urls;
    bool tables;
    bool task_lists;
    size_t max_nodes;
    size_t max_depth;
} TintaMarkdownOptions;

typedef struct TintaParseResult {
    TintaElement *root;
    bool success;
    char *error;
    size_t parse_time_us;
} TintaParseResult;

TintaMarkdownOptions tinta_markdown_default_options(void);
TintaParseResult tinta_markdown_parse(const char *markdown, size_t length,
                                      const TintaMarkdownOptions *options);
TintaParseResult tinta_markdown_parse_file(const char *path,
                                           const TintaMarkdownOptions *options);
void tinta_parse_result_destroy(TintaParseResult *result);

TintaElement *tinta_element_create(TintaElementType type);
void tinta_element_destroy(TintaElement *element);
bool tinta_element_add_child(TintaElement *parent, TintaElement *child);
const char *tinta_element_type_name(TintaElementType type);

#ifdef __cplusplus
}
#endif

#endif
