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
    size_t input_length;
    size_t current_source_begin;
    size_t current_source_end;
    bool failed;
    size_t max_depth;
} ParserContext;

static _Thread_local size_t current_node_limit;
static _Thread_local size_t current_node_count;

static uint64_t markdown_time_us(void) {
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) == TIME_UTC) {
        return (uint64_t)value.tv_sec * 1000000ULL +
               (uint64_t)value.tv_nsec / 1000ULL;
    }
    return (uint64_t)clock() * 1000000ULL / CLOCKS_PER_SEC;
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
    element->source_length = 0;
    element->text = empty_string();
    element->url = empty_string();
    element->title = empty_string();
    element->language = empty_string();
    element->raw = empty_string();
    if (!element->text || !element->url || !element->title ||
        !element->language || !element->raw) {
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
    free(element->raw);
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
    if (current->type == TINTA_ELEMENT_HTML_BLOCK ||
        current->type == TINTA_ELEMENT_MATH_INLINE ||
        current->type == TINTA_ELEMENT_MATH_DISPLAY) {
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
        text->source_offset = context->current_source_begin;
        text->source_length = context->current_source_begin == SIZE_MAX ? 0 :
            context->current_source_end - context->current_source_begin;
    }
    tinta_str8_clear(&context->current_text);
    context->current_source_begin = SIZE_MAX;
    context->current_source_end = SIZE_MAX;
    return true;
}

static bool context_append_text(ParserContext *context, const char *text,
                                size_t size,
                                const MD_SOURCE_RANGE *source) {
    size_t begin = SIZE_MAX;
    size_t end = SIZE_MAX;
    if (source && source->begin != MD_OFFSET_INVALID &&
        source->end >= source->begin) {
        begin = source->begin;
        end = source->end;
    }
    if (context->current_text.len &&
        (begin == SIZE_MAX || context->current_source_end == SIZE_MAX ||
         begin != context->current_source_end)) {
        if (!context_flush_text(context)) return false;
    }
    if (!context->current_text.len) {
        context->current_source_begin = begin;
        context->current_source_end = end;
    } else {
        context->current_source_end = end;
    }
    return tinta_str8_append(&context->current_text, text, size);
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

static bool context_add_leaf(ParserContext *context, TintaElementType type,
                             const MD_SOURCE_RANGE *source) {
    TintaElement *current = context_current(context);
    TintaElement *element = tinta_element_create(type);
    if (!current || !element || !tinta_element_add_child(current, element)) {
        tinta_element_destroy(element);
        return false;
    }
    if (source && source->begin != MD_OFFSET_INVALID &&
        source->end >= source->begin) {
        element->source_offset = source->begin;
        element->source_length = source->end - source->begin;
    }
    return true;
}

static int enter_block(MD_BLOCKTYPE type, void *detail,
                       const MD_SOURCE_RANGE *source, void *userdata) {
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
        case MD_BLOCK_UL: {
            MD_BLOCK_UL_DETAIL *list = (MD_BLOCK_UL_DETAIL *)detail;
            element = tinta_element_create(TINTA_ELEMENT_LIST);
            if (element) element->tight = list->is_tight != 0;
            break;
        }
        case MD_BLOCK_OL: {
            MD_BLOCK_OL_DETAIL *list = (MD_BLOCK_OL_DETAIL *)detail;
            element = tinta_element_create(TINTA_ELEMENT_LIST);
            if (element) {
                element->ordered = true;
                element->tight = list->is_tight != 0;
                element->start = (int)list->start;
            }
            break;
        }
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
        case MD_BLOCK_DETAILS:
            element = tinta_element_create(TINTA_ELEMENT_DETAILS);
            if (element)
                element->open = ((MD_BLOCK_DETAILS_DETAIL *)detail)->is_open != 0;
            break;
        case MD_BLOCK_SUMMARY:
            element = tinta_element_create(TINTA_ELEMENT_SUMMARY);
            break;
        default: return 0;
    }
    if (element && source && source->begin != MD_OFFSET_INVALID) {
        element->source_offset = source->begin;
        element->source_length = source->end >= source->begin ?
            source->end - source->begin : 0;
    }
    if (!element || !context_push(context, element)) goto failed;
    return 0;
failed:
    if (element && element->parent == NULL) tinta_element_destroy(element);
    context->failed = true;
    return 1;
}

static int leave_block(MD_BLOCKTYPE type, void *detail,
                       const MD_SOURCE_RANGE *source, void *userdata) {
    ParserContext *context = (ParserContext *)userdata;
    (void)detail;
    (void)source;
    if (!context_flush_text(context)) { context->failed = true; return 1; }
    switch (type) {
        case MD_BLOCK_DOC:
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY: return 0;
        default: context_pop(context); return 0;
    }
}

static int enter_span(MD_SPANTYPE type, void *detail,
                      const MD_SOURCE_RANGE *source, void *userdata) {
    ParserContext *context = (ParserContext *)userdata;
    TintaElement *element = NULL;
    if (!context_flush_text(context)) goto failed;
    switch (type) {
        case MD_SPAN_EM: element = tinta_element_create(TINTA_ELEMENT_EMPHASIS); break;
        case MD_SPAN_STRONG: element = tinta_element_create(TINTA_ELEMENT_STRONG); break;
        case MD_SPAN_DEL: element = tinta_element_create(TINTA_ELEMENT_STRIKETHROUGH); break;
        case MD_SPAN_MARK: element = tinta_element_create(TINTA_ELEMENT_HIGHLIGHT); break;
        case MD_SPAN_CODE: element = tinta_element_create(TINTA_ELEMENT_CODE); break;
        case MD_SPAN_SUBSCRIPT: element = tinta_element_create(TINTA_ELEMENT_SUBSCRIPT); break;
        case MD_SPAN_SUPERSCRIPT: element = tinta_element_create(TINTA_ELEMENT_SUPERSCRIPT); break;
        case MD_SPAN_RUBY: element = tinta_element_create(TINTA_ELEMENT_RUBY); break;
        case MD_SPAN_RUBY_TEXT: element = tinta_element_create(TINTA_ELEMENT_RUBY_TEXT); break;
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY: {
            MD_SPAN_LATEXMATH_DETAIL *math =
                (MD_SPAN_LATEXMATH_DETAIL *)detail;
            bool display = type == MD_SPAN_LATEXMATH_DISPLAY;
            element = tinta_element_create(display ?
                TINTA_ELEMENT_MATH_DISPLAY : TINTA_ELEMENT_MATH_INLINE);
            if (element && source && source->begin != MD_OFFSET_INVALID &&
                math && math->content.begin != MD_OFFSET_INVALID &&
                source->end >= source->begin &&
                math->content.end >= math->content.begin &&
                source->end <= context->input_length &&
                math->content.end <= context->input_length &&
                (!replace_string(&element->raw,
                    context->input_start + source->begin,
                    source->end - source->begin) ||
                 !replace_string(&element->text,
                    context->input_start + math->content.begin,
                    math->content.end - math->content.begin))) goto failed;
            break;
        }
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
    if (element && source && source->begin != MD_OFFSET_INVALID &&
        element->source_offset == SIZE_MAX) {
        element->source_offset = source->begin;
        element->source_length = source->end >= source->begin ?
            source->end - source->begin : 0;
    }
    if (!element || !context_push(context, element)) goto failed;
    return 0;
failed:
    if (element && !element->parent) tinta_element_destroy(element);
    context->failed = true;
    return 1;
}

static int leave_span(MD_SPANTYPE type, void *detail,
                      const MD_SOURCE_RANGE *source, void *userdata) {
    ParserContext *context = (ParserContext *)userdata;
    (void)detail;
    (void)source;
    if (!context_flush_text(context)) { context->failed = true; return 1; }
    switch (type) {
        case MD_SPAN_EM: case MD_SPAN_STRONG: case MD_SPAN_DEL:
        case MD_SPAN_MARK: case MD_SPAN_CODE:
        case MD_SPAN_SUBSCRIPT: case MD_SPAN_SUPERSCRIPT:
        case MD_SPAN_RUBY: case MD_SPAN_RUBY_TEXT:
        case MD_SPAN_LATEXMATH: case MD_SPAN_LATEXMATH_DISPLAY:
        case MD_SPAN_A: case MD_SPAN_IMG: context_pop(context); break;
        default: break;
    }
    return 0;
}

static int text_callback(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                         const MD_SOURCE_RANGE *source, void *userdata) {
    ParserContext *context = (ParserContext *)userdata;
    TintaElement *current = context_current(context);
    if (current && current->source_offset == SIZE_MAX && source &&
        source->begin != MD_OFFSET_INVALID) {
        current->source_offset = source->begin;
        current->source_length = source->end >= source->begin ?
            source->end - source->begin : 0;
    }
    switch (type) {
        case MD_TEXT_HTML:
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
            if (!context_append_text(context, text, size, source)) goto failed;
            break;
        case MD_TEXT_LATEXMATH:
            /* Exact math text was copied from the detail's content range. */
            break;
        case MD_TEXT_SOFTBR:
            if (!context_flush_text(context) || !context_add_leaf(context, TINTA_ELEMENT_SOFT_BREAK, source)) goto failed;
            break;
        case MD_TEXT_BR:
            if (!context_flush_text(context) || !context_add_leaf(context, TINTA_ELEMENT_HARD_BREAK, source)) goto failed;
            break;
        default:
            if (!context_append_text(context, text, size, source)) goto failed;
            break;
    }
    return 0;
failed:
    context->failed = true;
    return 1;
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
    context.input_length = length;
    context.current_source_begin = SIZE_MAX;
    context.current_source_end = SIZE_MAX;
    if (!context.root || !tinta_vec_push(&context.stack, &context.root)) goto failed;
    if (options.tables) flags |= MD_FLAG_TABLES;
    if (options.permissive_auto_links) flags |= MD_FLAG_PERMISSIVEAUTOLINKS;
    if (options.permissive_urls) flags |= MD_FLAG_PERMISSIVEURLAUTOLINKS;
    if (options.task_lists) flags |= MD_FLAG_TASKLISTS;
    flags |= MD_FLAG_LATEXMATHSPANS | MD_FLAG_TINTA_HTML |
             MD_FLAG_HIGHLIGHT | MD_FLAG_STRIKETHROUGH |
             MD_FLAG_SUPERSCRIPTS | MD_FLAG_SUBSCRIPTS;
    memset(&parser, 0, sizeof(parser));
    parser.abi_version = MD_PARSER_ABI_VERSION;
    parser.flags = flags;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = text_callback;
    start = markdown_time_us();
    status = md_parse(context.input_start, (MD_SIZE)length,
                      &parser, &context);
    {
        uint64_t elapsed = markdown_time_us() - start;
        result.parse_time_us = elapsed > (uint64_t)SIZE_MAX ?
            SIZE_MAX : (size_t)elapsed;
    }
    if (status || context.failed || !context_flush_text(&context)) goto failed;
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
        "HtmlBlock", "Text", "Code", "MathInline", "MathDisplay", "Emphasis", "Strong", "Link", "Image", "SoftBreak",
        "HardBreak", "Ruby", "RubyText", "Highlight", "Superscript", "Subscript", "Strikethrough",
        "Details", "Summary", "DetailsEnd"};
    return (unsigned)type < sizeof(names) / sizeof(names[0]) ? names[type] : "Unknown";
}
