#include "markdown.h"

#include <md4c.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct ParserContext {
    TintaElement *root;
    TintaVec stack;
    TintaStr8 current_text;
    const char *input_start;
    bool failed;
    bool suppress_rp_text;
    size_t max_depth;
} ParserContext;

static _Thread_local size_t current_node_limit;
static _Thread_local size_t current_node_count;

typedef struct ExtensionMatch {
    size_t start;
    size_t content_len;
    size_t delimiter_len;
    TintaElementType type;
} ExtensionMatch;

static uint64_t markdown_time_us(void) {
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) == TIME_UTC) {
        return (uint64_t)value.tv_sec * 1000000ULL +
               (uint64_t)value.tv_nsec / 1000ULL;
    }
    return (uint64_t)clock() * 1000000ULL / CLOCKS_PER_SEC;
}

typedef struct HtmlTag {
    TintaStr8 name;
    TintaStr8 href;
    TintaStr8 title;
    bool closing;
    bool self_closing;
} HtmlTag;

static bool is_space_char(char value) {
    return isspace((unsigned char)value) != 0;
}

static char *empty_string(void) { return tinta_str8_dup("", 0); }

TintaElement *tinta_element_create(TintaElementType type) {
    if (current_node_limit && current_node_count >= current_node_limit)
        return NULL;
    TintaElement *element = (TintaElement *)calloc(1, sizeof(*element));
    if (!element) return NULL;
    current_node_count++;
    element->type = type;
    element->start = 1;
    element->source_offset = SIZE_MAX;
    element->text = empty_string();
    element->url = empty_string();
    element->title = empty_string();
    element->language = empty_string();
    if (!element->text || !element->url || !element->title || !element->language) {
        tinta_element_destroy(element);
        return NULL;
    }
    return element;
}

void tinta_element_destroy(TintaElement *element) {
    size_t i;
    if (!element) return;
    for (i = 0; i < element->child_count; i++) tinta_element_destroy(element->children[i]);
    free(element->children);
    free(element->text);
    free(element->url);
    free(element->title);
    free(element->language);
    free(element);
}

bool tinta_element_add_child(TintaElement *parent, TintaElement *child) {
    TintaElement **children;
    size_t capacity;
    if (!parent || !child) return false;
    if (parent->child_count == parent->child_capacity) {
        capacity = parent->child_capacity ? parent->child_capacity * 2 : 4;
        if (capacity < parent->child_capacity || capacity > SIZE_MAX / sizeof(*children)) return false;
        children = (TintaElement **)realloc(parent->children, capacity * sizeof(*children));
        if (!children) return false;
        parent->children = children;
        parent->child_capacity = capacity;
    }
    child->parent = parent;
    parent->children[parent->child_count++] = child;
    return true;
}

static bool replace_string(char **target, const char *data, size_t length) {
    char *copy = tinta_str8_dup(data, length);
    if (!copy) return false;
    free(*target);
    *target = copy;
    return true;
}

static TintaElement *context_current(ParserContext *context) {
    if (!context->stack.len) return NULL;
    return TINTA_VEC_AT(TintaElement *, context->stack, context->stack.len - 1);
}

static bool context_flush_text(ParserContext *context) {
    TintaElement *current = context_current(context);
    if (!context->current_text.len || !current) return true;
    if (current->type == TINTA_ELEMENT_HTML_BLOCK) {
        size_t old = strlen(current->text);
        char *text = (char *)realloc(current->text, old + context->current_text.len + 1);
        if (!text) return false;
        current->text = text;
        memcpy(text + old, context->current_text.data, context->current_text.len);
        text[old + context->current_text.len] = 0;
    } else {
        TintaElement *text = tinta_element_create(TINTA_ELEMENT_TEXT);
        if (!text || !replace_string(&text->text, context->current_text.data,
                                     context->current_text.len) ||
            !tinta_element_add_child(current, text)) {
            tinta_element_destroy(text);
            return false;
        }
    }
    tinta_str8_clear(&context->current_text);
    return true;
}

static bool context_push(ParserContext *context, TintaElement *element) {
    TintaElement *current = context_current(context);
    if (!current ||
        (context->max_depth && context->stack.len >= context->max_depth) ||
        !tinta_element_add_child(current, element) ||
        !tinta_vec_push(&context->stack, &element)) return false;
    return true;
}

static void context_pop(ParserContext *context) {
    if (context->stack.len > 1) context->stack.len--;
}

static bool context_add_leaf(ParserContext *context, TintaElementType type) {
    TintaElement *current = context_current(context);
    TintaElement *element = tinta_element_create(type);
    if (!current || !element || !tinta_element_add_child(current, element)) {
        tinta_element_destroy(element);
        return false;
    }
    return true;
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
    ParserContext *context = (ParserContext *)userdata;
    TintaElement *element = NULL;
    if (!context_flush_text(context)) goto failed;
    switch (type) {
        case MD_BLOCK_DOC: return 0;
        case MD_BLOCK_P: element = tinta_element_create(TINTA_ELEMENT_PARAGRAPH); break;
        case MD_BLOCK_H:
            element = tinta_element_create(TINTA_ELEMENT_HEADING);
            if (element) element->level = ((MD_BLOCK_H_DETAIL *)detail)->level;
            break;
        case MD_BLOCK_CODE: {
            MD_BLOCK_CODE_DETAIL *code = (MD_BLOCK_CODE_DETAIL *)detail;
            element = tinta_element_create(TINTA_ELEMENT_CODE_BLOCK);
            if (element && code->lang.text && code->lang.size &&
                !replace_string(&element->language, code->lang.text, code->lang.size)) goto failed;
            break;
        }
        case MD_BLOCK_QUOTE: element = tinta_element_create(TINTA_ELEMENT_BLOCK_QUOTE); break;
        case MD_BLOCK_UL: element = tinta_element_create(TINTA_ELEMENT_LIST); break;
        case MD_BLOCK_OL:
            element = tinta_element_create(TINTA_ELEMENT_LIST);
            if (element) { element->ordered = true; element->start = (int)((MD_BLOCK_OL_DETAIL *)detail)->start; }
            break;
        case MD_BLOCK_LI: {
            MD_BLOCK_LI_DETAIL *item = (MD_BLOCK_LI_DETAIL *)detail;
            element = tinta_element_create(TINTA_ELEMENT_LIST_ITEM);
            if (element && item->is_task) {
                element->task = true;
                element->task_checked = item->task_mark == 'x' ||
                                        item->task_mark == 'X';
            }
            break;
        }
        case MD_BLOCK_HR: element = tinta_element_create(TINTA_ELEMENT_HORIZONTAL_RULE); break;
        case MD_BLOCK_TABLE:
            element = tinta_element_create(TINTA_ELEMENT_TABLE);
            if (element) element->col_count = (int)((MD_BLOCK_TABLE_DETAIL *)detail)->col_count;
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY: return 0;
        case MD_BLOCK_TR: element = tinta_element_create(TINTA_ELEMENT_TABLE_ROW); break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            element = tinta_element_create(TINTA_ELEMENT_TABLE_CELL);
            if (element) element->align = (int)((MD_BLOCK_TD_DETAIL *)detail)->align;
            break;
        case MD_BLOCK_HTML: element = tinta_element_create(TINTA_ELEMENT_HTML_BLOCK); break;
        default: return 0;
    }
    if (!element || !context_push(context, element)) goto failed;
    return 0;
failed:
    if (element && element->parent == NULL) tinta_element_destroy(element);
    context->failed = true;
    return 1;
}

static bool parse_html_into_elements(const char *html, TintaElement *parent);

static int leave_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
    ParserContext *context = (ParserContext *)userdata;
    (void)detail;
    if (!context_flush_text(context)) { context->failed = true; return 1; }
    switch (type) {
        case MD_BLOCK_DOC:
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY: return 0;
        case MD_BLOCK_HTML: {
            TintaElement *html = context_current(context);
            if (html && html->text[0] && !parse_html_into_elements(html->text, html)) {
                context->failed = true;
                return 1;
            }
            if (html && !replace_string(&html->text, "", 0)) {
                context->failed = true;
                return 1;
            }
            context_pop(context);
            return 0;
        }
        default: context_pop(context); return 0;
    }
}

static int enter_span(MD_SPANTYPE type, void *detail, void *userdata) {
    ParserContext *context = (ParserContext *)userdata;
    TintaElement *element = NULL;
    if (!context_flush_text(context)) goto failed;
    switch (type) {
        case MD_SPAN_EM: element = tinta_element_create(TINTA_ELEMENT_EMPHASIS); break;
        case MD_SPAN_STRONG: element = tinta_element_create(TINTA_ELEMENT_STRONG); break;
        case MD_SPAN_CODE: element = tinta_element_create(TINTA_ELEMENT_CODE); break;
        case MD_SPAN_A: {
            MD_SPAN_A_DETAIL *a = (MD_SPAN_A_DETAIL *)detail;
            element = tinta_element_create(TINTA_ELEMENT_LINK);
            if (element && a->href.text && !replace_string(&element->url, a->href.text, a->href.size)) goto failed;
            if (element && a->title.text && !replace_string(&element->title, a->title.text, a->title.size)) goto failed;
            break;
        }
        case MD_SPAN_IMG: {
            MD_SPAN_IMG_DETAIL *image = (MD_SPAN_IMG_DETAIL *)detail;
            element = tinta_element_create(TINTA_ELEMENT_IMAGE);
            if (element && image->src.text && !replace_string(&element->url, image->src.text, image->src.size)) goto failed;
            if (element && image->title.text && !replace_string(&element->title, image->title.text, image->title.size)) goto failed;
            break;
        }
        default: return 0;
    }
    if (!element || !context_push(context, element)) goto failed;
    return 0;
failed:
    if (element && !element->parent) tinta_element_destroy(element);
    context->failed = true;
    return 1;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
    ParserContext *context = (ParserContext *)userdata;
    (void)detail;
    if (!context_flush_text(context)) { context->failed = true; return 1; }
    switch (type) {
        case MD_SPAN_EM: case MD_SPAN_STRONG: case MD_SPAN_CODE:
        case MD_SPAN_A: case MD_SPAN_IMG: context_pop(context); break;
        default: break;
    }
    return 0;
}

static bool is_br_tag(const MD_CHAR *text, MD_SIZE size) {
    char normalized[8];
    size_t length = 0, i;
    if (size < 4 || size >= sizeof(normalized) || text[0] != '<' || text[size - 1] != '>') return false;
    for (i = 0; i < size; i++) if (!is_space_char(text[i])) normalized[length++] = (char)tolower((unsigned char)text[i]);
    normalized[length] = 0;
    return strcmp(normalized, "<br>") == 0 || strcmp(normalized, "<br/>") == 0;
}

static bool html_tag_eq(const MD_CHAR *text, MD_SIZE size, const char *name,
                        bool closing) {
    char normalized[24];
    size_t length = 0, i;
    if (size < 3 || size >= sizeof(normalized) || text[0] != '<' ||
        text[size - 1] != '>') return false;
    for (i = 0; i < size; i++)
        if (!is_space_char(text[i]))
            normalized[length++] = (char)tolower((unsigned char)text[i]);
    normalized[length] = 0;
    if (closing)
        return normalized[1] == '/' &&
            !strncmp(normalized + 2, name, strlen(name)) &&
            normalized[2 + strlen(name)] == '>';
    return normalized[1] != '/' &&
        !strncmp(normalized + 1, name, strlen(name)) &&
        (normalized[1 + strlen(name)] == '>' ||
         (normalized[1 + strlen(name)] == '/' &&
          normalized[2 + strlen(name)] == '>'));
}

static bool handle_inline_html_container(ParserContext *context,
                                         TintaElementType type,
                                         bool closing) {
    TintaElement *current;
    TintaElement *element;
    if (!context_flush_text(context)) return false;
    current = context_current(context);
    if (closing) {
        if (current && current->type == type) context_pop(context);
        return true;
    }
    element = tinta_element_create(type);
    if (!element) return false;
    if (!context_push(context, element)) {
        if (!element->parent) tinta_element_destroy(element);
        return false;
    }
    return true;
}

static int text_callback(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata) {
    ParserContext *context = (ParserContext *)userdata;
    TintaElement *current = context_current(context);
    if (context->input_start && current && current->source_offset == SIZE_MAX)
        current->source_offset = (size_t)(text - context->input_start);
    switch (type) {
        case MD_TEXT_HTML:
            if (current && current->type != TINTA_ELEMENT_HTML_BLOCK && is_br_tag(text, size)) {
                if (!context_flush_text(context) || !context_add_leaf(context, TINTA_ELEMENT_HARD_BREAK)) goto failed;
            } else if (current && current->type != TINTA_ELEMENT_HTML_BLOCK &&
                       (html_tag_eq(text,size,"ruby",false) ||
                        html_tag_eq(text,size,"ruby",true))) {
                if (!handle_inline_html_container(context,TINTA_ELEMENT_RUBY,
                        html_tag_eq(text,size,"ruby",true))) goto failed;
            } else if (current && current->type != TINTA_ELEMENT_HTML_BLOCK &&
                       (html_tag_eq(text,size,"rt",false) ||
                        html_tag_eq(text,size,"rt",true))) {
                if (!handle_inline_html_container(context,TINTA_ELEMENT_RUBY_TEXT,
                        html_tag_eq(text,size,"rt",true))) goto failed;
            } else if (current && current->type != TINTA_ELEMENT_HTML_BLOCK &&
                       html_tag_eq(text,size,"rp",false)) {
                if (!context_flush_text(context)) goto failed;
                context->suppress_rp_text = true;
            } else if (current && current->type != TINTA_ELEMENT_HTML_BLOCK &&
                       html_tag_eq(text,size,"rp",true)) {
                tinta_str8_clear(&context->current_text);
                context->suppress_rp_text = false;
            } else if (!tinta_str8_append(&context->current_text, text, size)) goto failed;
            break;
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
            if (!context->suppress_rp_text &&
                !tinta_str8_append(&context->current_text, text, size)) goto failed;
            break;
        case MD_TEXT_SOFTBR:
            if (!context_flush_text(context) || !context_add_leaf(context, TINTA_ELEMENT_SOFT_BREAK)) goto failed;
            break;
        case MD_TEXT_BR:
            if (!context_flush_text(context) || !context_add_leaf(context, TINTA_ELEMENT_HARD_BREAK)) goto failed;
            break;
        case MD_TEXT_ENTITY:
            if (size == 4 && memcmp(text, "&lt;", 4) == 0) text = "<", size = 1;
            else if (size == 4 && memcmp(text, "&gt;", 4) == 0) text = ">", size = 1;
            else if (size == 5 && memcmp(text, "&amp;", 5) == 0) text = "&", size = 1;
            else if (size == 6 && memcmp(text, "&quot;", 6) == 0) text = "\"", size = 1;
            else if (size == 6 && memcmp(text, "&nbsp;", 6) == 0) text = " ", size = 1;
            if (!tinta_str8_append(&context->current_text, text, size)) goto failed;
            break;
        default:
            if (!tinta_str8_append(&context->current_text, text, size)) goto failed;
            break;
    }
    return 0;
failed:
    context->failed = true;
    return 1;
}

static size_t find_bytes(const char *text, size_t length, const char *needle,
                         size_t needle_length, size_t from) {
    size_t i;
    if (!needle_length || from > length || needle_length > length - from) return SIZE_MAX;
    for (i = from; i + needle_length <= length; i++)
        if (memcmp(text + i, needle, needle_length) == 0) return i;
    return SIZE_MAX;
}

static bool find_extension(const char *text, size_t length, size_t from, ExtensionMatch *match) {
    size_t i;
    for (i = from; i < length; i++) {
        char c = text[i];
        if (c == '=' && i + 1 < length && text[i + 1] == '=') {
            size_t close = find_bytes(text, length, "==", 2, i + 2);
            if (close != SIZE_MAX && close > i + 2) {
                match->start = i; match->content_len = close - i - 2;
                match->delimiter_len = 2; match->type = TINTA_ELEMENT_HIGHLIGHT; return true;
            }
        } else if (c == '~' && i + 1 < length && text[i + 1] == '~') {
            size_t close = find_bytes(text, length, "~~", 2, i + 2);
            if (close != SIZE_MAX && close > i + 2) {
                match->start = i; match->content_len = close - i - 2;
                match->delimiter_len = 2; match->type = TINTA_ELEMENT_STRIKETHROUGH; return true;
            }
            if (find_bytes(text, length, "~", 1, i + 2) == SIZE_MAX) continue;
            i++;
        } else if (c == '^' || c == '~') {
            size_t close = find_bytes(text, length, &c, 1, i + 1), j;
            bool has_space = false;
            if (close == SIZE_MAX || close == i + 1) continue;
            for (j = i + 1; j < close; j++) if (text[j] == ' ' || text[j] == '\t' || text[j] == '\n') { has_space = true; break; }
            if (has_space) continue;
            match->start = i; match->content_len = close - i - 1;
            match->delimiter_len = 1;
            match->type = c == '^' ? TINTA_ELEMENT_SUPERSCRIPT : TINTA_ELEMENT_SUBSCRIPT;
            return true;
        }
    }
    return false;
}

static TintaElement *make_text(const char *text, size_t length) {
    TintaElement *element = tinta_element_create(TINTA_ELEMENT_TEXT);
    if (!element || !replace_string(&element->text, text, length)) {
        tinta_element_destroy(element);
        return NULL;
    }
    return element;
}

static bool split_extensions(TintaElement *parent) {
    TintaVec rebuilt;
    size_t i;
    bool changed = false;
    if (parent->type == TINTA_ELEMENT_CODE || parent->type == TINTA_ELEMENT_CODE_BLOCK ||
        parent->type == TINTA_ELEMENT_MERMAID_DIAGRAM) return true;
    tinta_vec_init(&rebuilt, sizeof(TintaElement *));
    for (i = 0; i < parent->child_count; i++) {
        TintaElement *child = parent->children[i];
        if (child->type != TINTA_ELEMENT_TEXT) {
            if (!split_extensions(child) || !tinta_vec_push(&rebuilt, &child)) goto failed;
        } else {
            size_t length = strlen(child->text), cursor = 0;
            ExtensionMatch match;
            bool any = false;
            while (find_extension(child->text, length, cursor, &match)) {
                TintaElement *span, *content;
                any = changed = true;
                if (match.start > cursor) {
                    TintaElement *prefix = make_text(child->text + cursor, match.start - cursor);
                    if (!prefix || !tinta_vec_push(&rebuilt, &prefix)) { tinta_element_destroy(prefix); goto failed; }
                    prefix->parent = parent;
                }
                span = tinta_element_create(match.type);
                content = make_text(child->text + match.start + match.delimiter_len, match.content_len);
                if (!span || !content || !tinta_element_add_child(span, content) ||
                    !tinta_vec_push(&rebuilt, &span)) {
                    if (content && !content->parent) tinta_element_destroy(content);
                    tinta_element_destroy(span);
                    goto failed;
                }
                span->parent = parent;
                cursor = match.start + match.delimiter_len * 2 + match.content_len;
            }
            if (!any) {
                if (!tinta_vec_push(&rebuilt, &child)) goto failed;
            } else {
                if (cursor < length) {
                    TintaElement *suffix = make_text(child->text + cursor, length - cursor);
                    if (!suffix || !tinta_vec_push(&rebuilt, &suffix)) { tinta_element_destroy(suffix); goto failed; }
                    suffix->parent = parent;
                }
                child->child_count = 0;
                tinta_element_destroy(child);
            }
        }
    }
    if (changed) {
        free(parent->children);
        parent->children = (TintaElement **)rebuilt.data;
        parent->child_count = rebuilt.len;
        parent->child_capacity = rebuilt.cap;
    } else tinta_vec_destroy(&rebuilt);
    return true;
failed:
    if (changed) {
        size_t j;
        for (j = 0; j < rebuilt.len; j++) {
            TintaElement *item = TINTA_VEC_AT(TintaElement *, rebuilt, j);
            bool belonged = false;
            for (i = 0; i < parent->child_count; i++) if (parent->children[i] == item) belonged = true;
            if (!belonged) tinta_element_destroy(item);
        }
    }
    tinta_vec_destroy(&rebuilt);
    return false;
}

static void remove_children(TintaElement *parent, size_t index, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) tinta_element_destroy(parent->children[index + i]);
    memmove(parent->children + index, parent->children + index + count,
            (parent->child_count - index - count) * sizeof(*parent->children));
    parent->child_count -= count;
}

static void detect_alerts(TintaElement *parent) {
    static const char *markers[] = {"[!note]", "[!tip]", "[!important]", "[!warning]", "[!caution]"};
    size_t i, marker_length;
    TintaElement *paragraph, *first;
    char *marker;
    int kind = 0;
    for (i = 0; i < parent->child_count; i++) detect_alerts(parent->children[i]);
    if (parent->type != TINTA_ELEMENT_BLOCK_QUOTE || !parent->child_count) return;
    paragraph = parent->children[0];
    if (paragraph->type != TINTA_ELEMENT_PARAGRAPH || !paragraph->child_count) return;
    first = paragraph->children[0];
    if (first->type != TINTA_ELEMENT_TEXT) return;
    marker_length = strlen(first->text);
    while (marker_length && (first->text[marker_length - 1] == ' ' || first->text[marker_length - 1] == '\t')) marker_length--;
    marker = tinta_str8_dup(first->text, marker_length);
    if (!marker) return;
    for (i = 0; i < marker_length; i++) marker[i] = (char)tolower((unsigned char)marker[i]);
    for (i = 0; i < 5; i++) if (strcmp(marker, markers[i]) == 0) { kind = (int)i + 1; break; }
    free(marker);
    if (!kind) return;
    if (paragraph->child_count > 1 && paragraph->children[1]->type != TINTA_ELEMENT_SOFT_BREAK &&
        paragraph->children[1]->type != TINTA_ELEMENT_HARD_BREAK) return;
    parent->alert_kind = kind;
    remove_children(paragraph, 0, paragraph->child_count < 2 ? 1 : 2);
    if (!paragraph->child_count) remove_children(parent, 0, 1);
}

TintaMarkdownOptions tinta_markdown_default_options(void) {
    TintaMarkdownOptions options;
    options.tab_width = 4;
    options.permissive_auto_links = true;
    options.permissive_urls = true;
    options.tables = true;
    options.task_lists = true;
    options.max_nodes = 0;
    options.max_depth = 0;
    return options;
}

TintaParseResult tinta_markdown_parse(const char *markdown, size_t length,
                                      const TintaMarkdownOptions *provided) {
    TintaParseResult result;
    TintaMarkdownOptions options = provided ? *provided : tinta_markdown_default_options();
    ParserContext context;
    MD_PARSER parser;
    unsigned flags = MD_FLAG_NOINDENTEDCODEBLOCKS;
    uint64_t start;
    int status;
    memset(&result, 0, sizeof(result));
    memset(&context, 0, sizeof(context));
    if (!markdown && length) { result.error = tinta_str8_dup("Invalid markdown", 16); return result; }
    current_node_limit = options.max_nodes;
    current_node_count = 0;
    context.max_depth = options.max_depth;
    context.root = tinta_element_create(TINTA_ELEMENT_DOCUMENT);
    tinta_vec_init(&context.stack, sizeof(TintaElement *));
    tinta_str8_init(&context.current_text);
    context.input_start = markdown ? markdown : "";
    if (!context.root || !tinta_vec_push(&context.stack, &context.root)) goto failed;
    if (options.tables) flags |= MD_FLAG_TABLES;
    if (options.permissive_auto_links) flags |= MD_FLAG_PERMISSIVEAUTOLINKS;
    if (options.permissive_urls) flags |= MD_FLAG_PERMISSIVEURLAUTOLINKS;
    if (options.task_lists) flags |= MD_FLAG_TASKLISTS;
    memset(&parser, 0, sizeof(parser));
    parser.flags = flags;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = text_callback;
    start = markdown_time_us();
    status = md_parse(context.input_start, (MD_SIZE)length, &parser, &context);
    {
        uint64_t elapsed = markdown_time_us() - start;
        result.parse_time_us = elapsed > (uint64_t)SIZE_MAX ?
            SIZE_MAX : (size_t)elapsed;
    }
    if (status || context.failed || !context_flush_text(&context) ||
        !split_extensions(context.root)) goto failed;
    detect_alerts(context.root);
    result.root = context.root;
    result.success = true;
    tinta_vec_destroy(&context.stack);
    tinta_str8_destroy(&context.current_text);
    current_node_limit = 0;
    current_node_count = 0;
    return result;
failed:
    tinta_element_destroy(context.root);
    tinta_vec_destroy(&context.stack);
    tinta_str8_destroy(&context.current_text);
    result.error = tinta_str8_dup("Failed to parse markdown", 24);
    current_node_limit = 0;
    current_node_count = 0;
    return result;
}

TintaParseResult tinta_markdown_parse_file(const char *path,
                                           const TintaMarkdownOptions *options) {
    TintaParseResult result;
    FILE *file;
    long size;
    char *data;
    memset(&result, 0, sizeof(result));
    file = fopen(path, "rb");
    if (!file) { result.error = tinta_str8_dup("Failed to open file", 19); return result; }
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file); result.error = tinta_str8_dup("Failed to read file", 19); return result;
    }
    data = (char *)malloc((size_t)size + 1);
    if (!data) { fclose(file); result.error = tinta_str8_dup("Out of memory", 13); return result; }
    if (size && fread(data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file); free(data); result.error = tinta_str8_dup("Failed to read file", 19); return result;
    }
    fclose(file); data[size] = 0;
    result = tinta_markdown_parse(data, (size_t)size, options);
    free(data);
    return result;
}

void tinta_parse_result_destroy(TintaParseResult *result) {
    if (!result) return;
    tinta_element_destroy(result->root);
    free(result->error);
    memset(result, 0, sizeof(*result));
}

const char *tinta_element_type_name(TintaElementType type) {
    static const char *names[] = {"Document", "Paragraph", "Heading", "CodeBlock", "MermaidDiagram",
        "BlockQuote", "List", "ListItem", "HorizontalRule", "Table", "TableRow", "TableCell",
        "HtmlBlock", "Text", "Code", "Emphasis", "Strong", "Link", "Image", "SoftBreak",
        "HardBreak", "Ruby", "RubyText", "Highlight", "Superscript", "Subscript", "Strikethrough"};
    return (unsigned)type < sizeof(names) / sizeof(names[0]) ? names[type] : "Unknown";
}

static bool html_tag_init(HtmlTag *tag) {
    memset(tag, 0, sizeof(*tag));
    return tinta_str8_init(&tag->name) && tinta_str8_init(&tag->href) && tinta_str8_init(&tag->title);
}

static void html_tag_destroy(HtmlTag *tag) {
    tinta_str8_destroy(&tag->name); tinta_str8_destroy(&tag->href); tinta_str8_destroy(&tag->title);
}

static bool extract_attribute(const char *tag, size_t length, const char *attribute, TintaStr8 *output) {
    size_t attribute_length = strlen(attribute), i;
    for (i = 1; i + attribute_length < length; i++) {
        size_t j, position;
        if (i > 1 && (isalnum((unsigned char)tag[i - 1]) || tag[i - 1] == '-' || tag[i - 1] == '_')) continue;
        for (j = 0; j < attribute_length; j++) if (tolower((unsigned char)tag[i + j]) != tolower((unsigned char)attribute[j])) break;
        if (j != attribute_length) continue;
        position = i + attribute_length;
        while (position < length && is_space_char(tag[position])) position++;
        if (position >= length || tag[position++] != '=') continue;
        while (position < length && is_space_char(tag[position])) position++;
        if (position < length && (tag[position] == '"' || tag[position] == '\'')) {
            char quote = tag[position++];
            size_t start = position;
            while (position < length && tag[position] != quote) position++;
            return tinta_str8_assign(output, tag + start, position - start);
        }
    }
    return true;
}

static bool parse_html_tag(const char *text, size_t length, HtmlTag *tag) {
    size_t start = 1, end, i;
    if (length > 1 && text[1] == '/') { tag->closing = true; start = 2; }
    end = start;
    while (end < length && !is_space_char(text[end]) && text[end] != '/' && text[end] != '>') end++;
    if (!tinta_str8_assign(&tag->name, text + start, end - start)) return false;
    for (i = 0; i < tag->name.len; i++) tag->name.data[i] = (char)tolower((unsigned char)tag->name.data[i]);
    tag->self_closing = length >= 2 && text[length - 2] == '/';
    return extract_attribute(text, length, "href", &tag->href) &&
           extract_attribute(text, length, "title", &tag->title);
}

static TintaElementType html_element_type(const char *name, int *heading_level) {
    *heading_level = 0;
    if (!strcmp(name, "ul") || !strcmp(name, "ol")) return TINTA_ELEMENT_LIST;
    if (!strcmp(name, "li")) return TINTA_ELEMENT_LIST_ITEM;
    if (!strcmp(name, "a")) return TINTA_ELEMENT_LINK;
    if (!strcmp(name, "strong") || !strcmp(name, "b")) return TINTA_ELEMENT_STRONG;
    if (!strcmp(name, "em") || !strcmp(name, "i")) return TINTA_ELEMENT_EMPHASIS;
    if (!strcmp(name, "code")) return TINTA_ELEMENT_CODE;
    if (!strcmp(name, "p")) return TINTA_ELEMENT_PARAGRAPH;
    if (strlen(name) == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
        *heading_level = name[1] - '0'; return TINTA_ELEMENT_HEADING;
    }
    if (!strcmp(name, "pre")) return TINTA_ELEMENT_CODE_BLOCK;
    if (!strcmp(name, "blockquote")) return TINTA_ELEMENT_BLOCK_QUOTE;
    if (!strcmp(name, "ruby")) return TINTA_ELEMENT_RUBY;
    if (!strcmp(name, "rt")) return TINTA_ELEMENT_RUBY_TEXT;
    return TINTA_ELEMENT_DOCUMENT;
}

static bool html_flush_text(TintaStr8 *buffer, TintaVec *stack) {
    size_t start = 0, end = buffer->len;
    TintaElement *parent, *text;
    while (start < end && is_space_char(buffer->data[start])) start++;
    while (end > start && is_space_char(buffer->data[end - 1])) end--;
    if (start == end) { tinta_str8_clear(buffer); return true; }
    parent = TINTA_VEC_AT(TintaElement *, *stack, stack->len - 1);
    text = make_text(buffer->data + start, end - start);
    tinta_str8_clear(buffer);
    if (!text || !tinta_element_add_child(parent, text)) { tinta_element_destroy(text); return false; }
    return true;
}

static bool parse_html_into_elements(const char *html, TintaElement *parent) {
    TintaVec stack;
    TintaStr8 buffer;
    size_t length = strlen(html), position = 0;
    tinta_vec_init(&stack, sizeof(TintaElement *));
    tinta_str8_init(&buffer);
    if (!tinta_vec_push(&stack, &parent)) goto failed;
    while (position < length) {
        size_t tag_start = find_bytes(html, length, "<", 1, position), tag_end;
        HtmlTag tag;
        int heading_level;
        TintaElementType type;
        if (tag_start == SIZE_MAX) { if (!tinta_str8_append(&buffer, html + position, length - position)) goto failed; break; }
        if (tag_start > position && !tinta_str8_append(&buffer, html + position, tag_start - position)) goto failed;
        if (!memcmp(html + tag_start, "<!--", tag_start + 4 <= length ? 4 : 0)) {
            size_t comment_end = find_bytes(html, length, "-->", 3, tag_start + 4);
            if (comment_end == SIZE_MAX) break;
            position = comment_end + 3;
            continue;
        }
        tag_end = find_bytes(html, length, ">", 1, tag_start + 1);
        if (tag_end == SIZE_MAX) { if (!tinta_str8_append(&buffer, html + tag_start, length - tag_start)) goto failed; break; }
        html_tag_init(&tag);
        if (!parse_html_tag(html + tag_start, tag_end - tag_start + 1, &tag)) { html_tag_destroy(&tag); goto failed; }
        if (!strcmp(tag.name.data, "br") || !strcmp(tag.name.data, "hr")) {
            TintaElement *leaf;
            if (!html_flush_text(&buffer, &stack)) { html_tag_destroy(&tag); goto failed; }
            leaf = tinta_element_create(!strcmp(tag.name.data, "br") ? TINTA_ELEMENT_HARD_BREAK : TINTA_ELEMENT_HORIZONTAL_RULE);
            if (!leaf || !tinta_element_add_child(TINTA_VEC_AT(TintaElement *, stack, stack.len - 1), leaf)) {
                tinta_element_destroy(leaf); html_tag_destroy(&tag); goto failed;
            }
        } else if (!strcmp(tag.name.data, "rp")) {
            if (!tag.closing) { if (!html_flush_text(&buffer, &stack)) { html_tag_destroy(&tag); goto failed; } }
            else tinta_str8_clear(&buffer);
        } else {
            type = html_element_type(tag.name.data, &heading_level);
            if (type != TINTA_ELEMENT_DOCUMENT) {
                if (!html_flush_text(&buffer, &stack)) { html_tag_destroy(&tag); goto failed; }
                if (!tag.closing) {
                    TintaElement *element = tinta_element_create(type);
                    TintaElement *current = TINTA_VEC_AT(TintaElement *, stack, stack.len - 1);
                    if (!element) { html_tag_destroy(&tag); goto failed; }
                    element->level = heading_level;
                    if (type == TINTA_ELEMENT_LIST) element->ordered = strcmp(tag.name.data, "ol") == 0;
                    if (type == TINTA_ELEMENT_LINK &&
                        (!replace_string(&element->url, tag.href.data ? tag.href.data : "", tag.href.len) ||
                         !replace_string(&element->title, tag.title.data ? tag.title.data : "", tag.title.len))) {
                        tinta_element_destroy(element); html_tag_destroy(&tag); goto failed;
                    }
                    if (!tinta_element_add_child(current, element) ||
                        (!tag.self_closing && !tinta_vec_push(&stack, &element))) {
                        if (!element->parent) tinta_element_destroy(element);
                        html_tag_destroy(&tag); goto failed;
                    }
                } else if (stack.len > 1) stack.len--;
            }
        }
        html_tag_destroy(&tag);
        position = tag_end + 1;
    }
    if (!html_flush_text(&buffer, &stack)) goto failed;
    tinta_vec_destroy(&stack); tinta_str8_destroy(&buffer); return true;
failed:
    tinta_vec_destroy(&stack); tinta_str8_destroy(&buffer); return false;
}
