#include "mermaid.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct View {
    const char *data;
    size_t len;
} View;

typedef struct NodeSpec {
    TintaStr8 id;
    TintaStr8 label;
    TintaStr8 class_name;
    TintaMermaidNodeShape shape;
    bool has_definition;
    size_t source_offset;
    size_t parent_subgraph;
} NodeSpec;

typedef struct ArrowSpec {
    bool directed;
    bool dashed;
    float stroke_scale;
    TintaStr8 label;
} ArrowSpec;

typedef struct Delimiter {
    const char *open;
    const char *close;
    TintaMermaidNodeShape shape;
} Delimiter;

typedef struct ParseState {
    TintaVec nodes;
    TintaVec edges;
    TintaVec raw_edges;
    TintaVec subgraphs;
    TintaVec class_styles;
    TintaVec style_assignments;
    TintaVec class_assignments;
    TintaVec subgraph_stack;
    TintaStringMap node_ids;
    TintaStringMap subgraph_ids;
    TintaStringMap class_ids;
    TintaMermaidDirection direction;
    size_t max_nodes;
    size_t max_edges;
} ParseState;

typedef struct RawEdge {
    char *from_id;
    char *to_id;
    char *label;
    bool directed;
    bool dashed;
    float stroke_scale;
} RawEdge;

typedef struct StyleAssignment {
    char *id;
    TintaMermaidStyle style;
} StyleAssignment;

typedef struct ClassAssignment {
    char *id;
    char *class_name;
    size_t source_offset;
} ClassAssignment;

typedef struct LayoutEdge {
    size_t from;
    size_t to;
} LayoutEdge;

typedef struct IndexList {
    TintaVec values;
} IndexList;

static const Delimiter DELIMITERS[] = {
    {"((", "))", TINTA_MERMAID_CIRCLE},
    {"([", "])", TINTA_MERMAID_STADIUM},
    {"[(", ")]", TINTA_MERMAID_ROUNDED_RECTANGLE},
    {"[[", "]]", TINTA_MERMAID_RECTANGLE},
    {"{{", "}}", TINTA_MERMAID_HEXAGON},
    {"[/", "/]", TINTA_MERMAID_RECTANGLE},
    {"[\\", "\\]", TINTA_MERMAID_RECTANGLE},
    {"(", ")", TINTA_MERMAID_ROUNDED_RECTANGLE},
    {"{", "}", TINTA_MERMAID_DIAMOND},
    {"[", "]", TINTA_MERMAID_RECTANGLE}
};

static float maxf(float a, float b) { return a > b ? a : b; }

static bool is_space(char c) {
    return isspace((unsigned char)c) != 0;
}

static char lower_ascii(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static View view_make(const char *data, size_t len) {
    View value;
    value.data = data;
    value.len = len;
    return value;
}

static View trim(View value) {
    while (value.len && is_space(value.data[0])) {
        value.data++;
        value.len--;
    }
    while (value.len && is_space(value.data[value.len - 1])) value.len--;
    return value;
}

static bool view_eq(View left, View right) {
    return left.len == right.len &&
           (left.len == 0 || memcmp(left.data, right.data, left.len) == 0);
}

static bool view_eq_text(View value, const char *text) {
    return view_eq(value, view_make(text, strlen(text)));
}

static bool view_eq_icase(View left, View right) {
    size_t i;
    if (left.len != right.len) return false;
    for (i = 0; i < left.len; i++) {
        if (lower_ascii(left.data[i]) != lower_ascii(right.data[i])) return false;
    }
    return true;
}

static bool view_eq_text_icase(View value, const char *text) {
    return view_eq_icase(value, view_make(text, strlen(text)));
}

static bool starts_at(View value, size_t position, const char *text) {
    size_t length = strlen(text);
    return position <= value.len && value.len - position >= length &&
           memcmp(value.data + position, text, length) == 0;
}

static size_t find_text(View value, const char *text, size_t position) {
    size_t length = strlen(text);
    size_t i;
    if (length == 0) return position <= value.len ? position : SIZE_MAX;
    if (position > value.len || length > value.len - position) return SIZE_MAX;
    for (i = position; i + length <= value.len; i++) {
        if (memcmp(value.data + i, text, length) == 0) return i;
    }
    return SIZE_MAX;
}

static size_t find_char(View value, char needle, size_t position) {
    size_t i;
    for (i = position; i < value.len; i++) {
        if (value.data[i] == needle) return i;
    }
    return SIZE_MAX;
}

static void skip_spaces(View value, size_t *position) {
    while (*position < value.len && is_space(value.data[*position])) (*position)++;
}

static View read_word(View value, size_t *position) {
    size_t start;
    skip_spaces(value, position);
    start = *position;
    while (*position < value.len && !is_space(value.data[*position])) (*position)++;
    return view_make(value.data + start, *position - start);
}

static bool is_arrow_at(View value, size_t position) {
    return starts_at(value, position, "-->") || starts_at(value, position, "---") ||
           starts_at(value, position, "-.->") || starts_at(value, position, "==>");
}

static bool set_error(char **error, const char *message) {
    free(*error);
    *error = tinta_str8_dup(message, strlen(message));
    return false;
}

static bool decode_label(View encoded, TintaStr8 *decoded) {
    size_t i;
    static const struct {
        const char *encoded;
        const char *decoded;
    } entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&#39;", "'"}
    };
    tinta_str8_clear(decoded);
    if (!tinta_str8_reserve(decoded, encoded.len)) return false;
    for (i = 0; i < encoded.len;) {
        size_t e;
        if (encoded.data[i] == '\\' && i + 1 < encoded.len) {
            char next = encoded.data[i + 1];
            if (next == '"' || next == '\'' || next == '\\') {
                if (!tinta_str8_append_char(decoded, next)) return false;
                i += 2;
                continue;
            }
            if (next == 'n') {
                if (!tinta_str8_append_char(decoded, '\n')) return false;
                i += 2;
                continue;
            }
        }
        if (encoded.data[i] == '<') {
            size_t close = find_char(encoded, '>', i + 1);
            if (close != SIZE_MAX) {
                View tag = trim(view_make(encoded.data + i + 1, close - i - 1));
                if (tag.len && tag.data[tag.len - 1] == '/') {
                    tag.len--;
                    tag = trim(tag);
                }
                if (view_eq_text_icase(tag, "br")) {
                    if (!tinta_str8_append_char(decoded, '\n')) return false;
                    i = close + 1;
                    continue;
                }
            }
        }
        for (e = 0; e < sizeof(entities) / sizeof(entities[0]); e++) {
            if (starts_at(encoded, i, entities[e].encoded)) {
                if (!tinta_str8_append(decoded, entities[e].decoded,
                                       strlen(entities[e].decoded))) return false;
                i += strlen(entities[e].encoded);
                break;
            }
        }
        if (e != sizeof(entities) / sizeof(entities[0])) continue;
        if (!tinta_str8_append_char(decoded, encoded.data[i])) return false;
        i++;
    }
    return true;
}

static bool parse_hex_digit(char c, uint32_t *value) {
    if (c >= '0' && c <= '9') {
        *value = (uint32_t)(c - '0');
        return true;
    }
    c = lower_ascii(c);
    if (c >= 'a' && c <= 'f') {
        *value = (uint32_t)(c - 'a' + 10);
        return true;
    }
    return false;
}

static bool parse_color(View value, TintaMermaidColor *color) {
    size_t i;
    static const struct { const char *name; uint32_t rgb; } named[] = {
        {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000},
        {"green", 0x008000}, {"blue", 0x0000FF}, {"gray", 0x808080},
        {"grey", 0x808080}, {"yellow", 0xFFFF00}, {"orange", 0xFFA500},
        {"purple", 0x800080}
    };
    value = trim(value);
    if (view_eq_text_icase(value, "transparent") || view_eq_text_icase(value, "none")) {
        color->rgb = 0;
        color->alpha = 0.0f;
        return true;
    }
    for (i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        if (view_eq_text_icase(value, named[i].name)) {
            color->rgb = named[i].rgb;
            color->alpha = 1.0f;
            return true;
        }
    }
    if (!value.len || value.data[0] != '#') return false;
    value.data++;
    value.len--;
    if (value.len == 3 || value.len == 4) {
        uint32_t c[4] = {0};
        for (i = 0; i < value.len; i++) {
            if (!parse_hex_digit(value.data[i], &c[i])) return false;
            c[i] *= 17;
        }
        color->rgb = (c[0] << 16) | (c[1] << 8) | c[2];
        color->alpha = value.len == 4 ? c[3] / 255.0f : 1.0f;
        return true;
    }
    if (value.len == 6 || value.len == 8) {
        uint32_t c[8] = {0};
        uint32_t b0, b1, b2, b3 = 255;
        for (i = 0; i < value.len; i++) if (!parse_hex_digit(value.data[i], &c[i])) return false;
        b0 = (c[0] << 4) | c[1];
        b1 = (c[2] << 4) | c[3];
        b2 = (c[4] << 4) | c[5];
        if (value.len == 8) b3 = (c[6] << 4) | c[7];
        color->rgb = (b0 << 16) | (b1 << 8) | b2;
        color->alpha = b3 / 255.0f;
        return true;
    }
    return false;
}

static bool parse_stroke_width(View value, float *width) {
    TintaStr8 text = {0};
    char *end;
    float parsed;
    value = trim(value);
    if (!value.len || !tinta_str8_assign(&text, value.data, value.len)) return false;
    errno = 0;
    end = NULL;
    parsed = strtof(text.data, &end);
    if (end == text.data || errno == ERANGE || !isfinite(parsed) || parsed < 0.0f) {
        tinta_str8_destroy(&text);
        return false;
    }
    while (*end && is_space(*end)) end++;
    if (*end && strcmp(end, "px") != 0) {
        tinta_str8_destroy(&text);
        return false;
    }
    *width = parsed;
    tinta_str8_destroy(&text);
    return true;
}

static void merge_style(TintaMermaidStyle *target, const TintaMermaidStyle *source) {
    if (source->has_fill) { target->has_fill = true; target->fill = source->fill; }
    if (source->has_stroke) { target->has_stroke = true; target->stroke = source->stroke; }
    if (source->has_text) { target->has_text = true; target->text = source->text; }
    if (source->has_stroke_width) {
        target->has_stroke_width = true;
        target->stroke_width = source->stroke_width;
    }
}

static bool parse_style_list(View value, TintaMermaidStyle *style, char **error) {
    size_t position = 0;
    while (position <= value.len) {
        size_t comma = find_char(value, ',', position);
        View item;
        size_t colon;
        View key;
        View style_value;
        TintaMermaidColor color;
        if (comma == SIZE_MAX) comma = value.len;
        item = trim(view_make(value.data + position, comma - position));
        if (item.len) {
            colon = find_char(item, ':', 0);
            if (colon == SIZE_MAX) return set_error(error, "Expected ':' in style declaration");
            key = trim(view_make(item.data, colon));
            style_value = trim(view_make(item.data + colon + 1, item.len - colon - 1));
            if (view_eq_text_icase(key, "fill")) {
                if (!parse_color(style_value, &color)) return set_error(error, "Invalid fill color");
                style->has_fill = true; style->fill = color;
            } else if (view_eq_text_icase(key, "stroke")) {
                if (!parse_color(style_value, &color)) return set_error(error, "Invalid stroke color");
                style->has_stroke = true; style->stroke = color;
            } else if (view_eq_text_icase(key, "color")) {
                if (!parse_color(style_value, &color)) return set_error(error, "Invalid text color");
                style->has_text = true; style->text = color;
            } else if (view_eq_text_icase(key, "stroke-width")) {
                if (!parse_stroke_width(style_value, &style->stroke_width))
                    return set_error(error, "Invalid stroke width");
                style->has_stroke_width = true;
            }
        }
        if (comma == value.len) break;
        position = comma + 1;
    }
    return true;
}

static void node_spec_destroy(NodeSpec *spec) {
    tinta_str8_destroy(&spec->id);
    tinta_str8_destroy(&spec->label);
    tinta_str8_destroy(&spec->class_name);
    memset(spec, 0, sizeof(*spec));
}

static bool node_spec_init(NodeSpec *spec) {
    memset(spec, 0, sizeof(*spec));
    spec->shape = TINTA_MERMAID_RECTANGLE;
    spec->parent_subgraph = SIZE_MAX;
    return tinta_str8_init(&spec->id) && tinta_str8_init(&spec->label) &&
           tinta_str8_init(&spec->class_name);
}

static bool parse_node_spec(View line, size_t *position, size_t source_offset,
                            NodeSpec *spec, char **error) {
    size_t id_start;
    const Delimiter *delimiter = NULL;
    size_t d;
    skip_spaces(line, position);
    id_start = *position;
    while (*position < line.len) {
        char c = line.data[*position];
        if (is_space(c) || c == '[' || c == '(' || c == '{' || c == ':' ||
            c == ';' || c == ',') break;
        if (is_arrow_at(line, *position)) break;
        (*position)++;
    }
    if (*position == id_start) return set_error(error, "Expected a node identifier");
    if (!tinta_str8_assign(&spec->id, line.data + id_start, *position - id_start) ||
        !tinta_str8_assign(&spec->label, line.data + id_start, *position - id_start))
        return set_error(error, "Out of memory");
    spec->source_offset = source_offset + id_start;
    skip_spaces(line, position);
    if (spec->id.len && spec->id.data[spec->id.len - 1] == '@' &&
        *position < line.len && line.data[*position] == '{')
        return set_error(error, "Mermaid '@{ }' attribute syntax is not supported");
    for (d = 0; d < sizeof(DELIMITERS) / sizeof(DELIMITERS[0]); d++) {
        if (starts_at(line, *position, DELIMITERS[d].open)) {
            delimiter = &DELIMITERS[d];
            break;
        }
    }
    if (delimiter) {
        View encoded;
        spec->has_definition = true;
        spec->shape = delimiter->shape;
        *position += strlen(delimiter->open);
        skip_spaces(line, position);
        if (*position < line.len && (line.data[*position] == '"' || line.data[*position] == '\'')) {
            char quote = line.data[(*position)++];
            size_t label_start = *position;
            bool escaped = false;
            while (*position < line.len) {
                char c = line.data[*position];
                if (c == quote && !escaped) break;
                escaped = c == '\\' && !escaped;
                if (c != '\\') escaped = false;
                (*position)++;
            }
            if (*position >= line.len) return set_error(error, "Unterminated quoted node label");
            encoded = view_make(line.data + label_start, *position - label_start);
            (*position)++;
            skip_spaces(line, position);
            if (!starts_at(line, *position, delimiter->close))
                return set_error(error, "Expected closing node delimiter");
            *position += strlen(delimiter->close);
        } else {
            size_t close = find_text(line, delimiter->close, *position);
            if (close == SIZE_MAX) return set_error(error, "Unterminated node label");
            encoded = trim(view_make(line.data + *position, close - *position));
            *position = close + strlen(delimiter->close);
        }
        if (!decode_label(encoded, &spec->label)) return set_error(error, "Out of memory");
    }
    skip_spaces(line, position);
    if (starts_at(line, *position, ":::")) {
        size_t class_start;
        *position += 3;
        class_start = *position;
        while (*position < line.len && !is_space(line.data[*position]) &&
               line.data[*position] != ';' && !is_arrow_at(line, *position)) (*position)++;
        if (*position == class_start) return set_error(error, "Expected a class name after ':::'");
        if (!tinta_str8_assign(&spec->class_name, line.data + class_start,
                               *position - class_start)) return set_error(error, "Out of memory");
    }
    return true;
}

static void arrow_spec_destroy(ArrowSpec *arrow) {
    tinta_str8_destroy(&arrow->label);
}

static bool arrow_spec_init(ArrowSpec *arrow) {
    memset(arrow, 0, sizeof(*arrow));
    arrow->directed = true;
    arrow->stroke_scale = 1.0f;
    return tinta_str8_init(&arrow->label);
}

static bool parse_arrow(View line, size_t *position, ArrowSpec *arrow, char **error) {
    skip_spaces(line, position);
    if (starts_at(line, *position, "-.->")) {
        arrow->dashed = true; *position += 4;
    } else if (starts_at(line, *position, "==>")) {
        arrow->stroke_scale = 2.0f; *position += 3;
    } else if (starts_at(line, *position, "-->")) {
        *position += 3;
    } else if (starts_at(line, *position, "---")) {
        arrow->directed = false; *position += 3;
    } else if (starts_at(line, *position, "--")) {
        size_t end_arrow = find_text(line, "-->", *position + 2);
        View label;
        if (end_arrow == SIZE_MAX) return set_error(error, "Unsupported edge syntax");
        label = trim(view_make(line.data + *position + 2, end_arrow - *position - 2));
        if (!tinta_str8_assign(&arrow->label, label.data, label.len)) return set_error(error, "Out of memory");
        *position = end_arrow + 3;
    } else return set_error(error, "Expected a Mermaid edge");
    skip_spaces(line, position);
    if (*position < line.len && line.data[*position] == '|') {
        size_t label_end = find_char(line, '|', *position + 1);
        if (label_end == SIZE_MAX) return set_error(error, "Unterminated edge label");
        if (!decode_label(view_make(line.data + *position + 1, label_end - *position - 1),
                          &arrow->label)) return set_error(error, "Out of memory");
        *position = label_end + 1;
    }
    return true;
}

static void node_destroy(TintaMermaidNode *node) {
    free(node->id);
    free(node->label);
    free(node->class_name);
    memset(node, 0, sizeof(*node));
}

static void edge_destroy(TintaMermaidEdge *edge) {
    free(edge->label);
    memset(edge, 0, sizeof(*edge));
}

static void raw_edge_destroy(RawEdge *edge) {
    free(edge->from_id);
    free(edge->to_id);
    free(edge->label);
    memset(edge, 0, sizeof(*edge));
}

static void subgraph_destroy(TintaMermaidSubgraph *subgraph) {
    free(subgraph->id);
    free(subgraph->label);
    free(subgraph->class_name);
    memset(subgraph, 0, sizeof(*subgraph));
}

static void style_assignment_destroy(StyleAssignment *assignment) {
    free(assignment->id);
    memset(assignment, 0, sizeof(*assignment));
}

static void class_assignment_destroy(ClassAssignment *assignment) {
    free(assignment->id);
    free(assignment->class_name);
    memset(assignment, 0, sizeof(*assignment));
}

static void class_style_destroy(TintaMermaidClassStyle *style) {
    free(style->name);
    memset(style, 0, sizeof(*style));
}

static bool parse_state_init(ParseState *state) {
    memset(state, 0, sizeof(*state));
    state->direction = TINTA_MERMAID_TOP_TO_BOTTOM;
    return tinta_vec_init(&state->nodes, sizeof(TintaMermaidNode)) &&
           tinta_vec_init(&state->edges, sizeof(TintaMermaidEdge)) &&
           tinta_vec_init(&state->raw_edges, sizeof(RawEdge)) &&
           tinta_vec_init(&state->subgraphs, sizeof(TintaMermaidSubgraph)) &&
           tinta_vec_init(&state->class_styles, sizeof(TintaMermaidClassStyle)) &&
           tinta_vec_init(&state->style_assignments, sizeof(StyleAssignment)) &&
           tinta_vec_init(&state->class_assignments, sizeof(ClassAssignment)) &&
           tinta_vec_init(&state->subgraph_stack, sizeof(size_t)) &&
           tinta_map_init(&state->node_ids) &&
           tinta_map_init(&state->subgraph_ids) &&
           tinta_map_init(&state->class_ids);
}

static void parse_state_destroy(ParseState *state) {
    size_t i;
    for (i = 0; i < state->nodes.len; i++) node_destroy(TINTA_VEC_PTR(TintaMermaidNode, state->nodes, i));
    for (i = 0; i < state->edges.len; i++) edge_destroy(TINTA_VEC_PTR(TintaMermaidEdge, state->edges, i));
    for (i = 0; i < state->raw_edges.len; i++) raw_edge_destroy(TINTA_VEC_PTR(RawEdge, state->raw_edges, i));
    for (i = 0; i < state->subgraphs.len; i++) subgraph_destroy(TINTA_VEC_PTR(TintaMermaidSubgraph, state->subgraphs, i));
    for (i = 0; i < state->class_styles.len; i++) class_style_destroy(TINTA_VEC_PTR(TintaMermaidClassStyle, state->class_styles, i));
    for (i = 0; i < state->style_assignments.len; i++) style_assignment_destroy(TINTA_VEC_PTR(StyleAssignment, state->style_assignments, i));
    for (i = 0; i < state->class_assignments.len; i++) class_assignment_destroy(TINTA_VEC_PTR(ClassAssignment, state->class_assignments, i));
    tinta_vec_destroy(&state->nodes);
    tinta_vec_destroy(&state->edges);
    tinta_vec_destroy(&state->raw_edges);
    tinta_vec_destroy(&state->subgraphs);
    tinta_vec_destroy(&state->class_styles);
    tinta_vec_destroy(&state->style_assignments);
    tinta_vec_destroy(&state->class_assignments);
    tinta_vec_destroy(&state->subgraph_stack);
    tinta_map_destroy(&state->node_ids);
    tinta_map_destroy(&state->subgraph_ids);
    tinta_map_destroy(&state->class_ids);
}

static bool ensure_node(ParseState *state, const NodeSpec *spec, size_t *index,
                        char **error) {
    size_t existing;
    if (tinta_map_get(&state->subgraph_ids, spec->id.data,
                      spec->id.len, &existing)) {
        if (spec->has_definition)
            return set_error(error,
                "A Mermaid identifier cannot name both a node and a subgraph");
        *index = SIZE_MAX;
        return true;
    }
    if (tinta_map_get(&state->node_ids, spec->id.data, spec->id.len, &existing)) {
        TintaMermaidNode *node = TINTA_VEC_PTR(TintaMermaidNode, state->nodes, existing);
        if (spec->parent_subgraph != SIZE_MAX) {
            if (node->parent_subgraph == SIZE_MAX) {
                if (node->has_definition && spec->has_definition)
                    return set_error(error,
                        "A Mermaid node cannot belong to multiple subgraphs");
                if (!node->has_definition || spec->has_definition)
                    node->parent_subgraph = spec->parent_subgraph;
            }
            else if (node->parent_subgraph != spec->parent_subgraph &&
                     spec->has_definition)
                return set_error(error,
                    "A Mermaid node cannot belong to multiple subgraphs");
        }
        if (spec->has_definition) {
            char *label = tinta_str8_dup(spec->label.data, spec->label.len);
            if (!label) return set_error(error, "Out of memory");
            free(node->label);
            node->label = label;
            node->shape = spec->shape;
            node->has_definition = true;
        }
        if (spec->class_name.len) {
            char *class_name = tinta_str8_dup(spec->class_name.data, spec->class_name.len);
            if (!class_name) return set_error(error, "Out of memory");
            free(node->class_name);
            node->class_name = class_name;
        }
        if (spec->source_offset < node->source_offset) node->source_offset = spec->source_offset;
        *index = existing;
        return true;
    }
    {
        TintaMermaidNode node;
        if (state->max_nodes &&
            state->nodes.len + state->subgraphs.len >= state->max_nodes)
            return set_error(error, "Mermaid node limit exceeded");
        memset(&node, 0, sizeof(node));
        node.id = tinta_str8_dup(spec->id.data, spec->id.len);
        node.label = tinta_str8_dup(spec->label.data, spec->label.len);
        node.class_name = tinta_str8_dup(spec->class_name.data, spec->class_name.len);
        node.shape = spec->shape;
        node.source_offset = spec->source_offset;
        node.parent_subgraph = spec->parent_subgraph;
        node.has_definition = spec->has_definition;
        node.style.stroke_width = 1.0f;
        if (!node.id || !node.label || !node.class_name) {
            node_destroy(&node);
            return set_error(error, "Out of memory");
        }
        *index = state->nodes.len;
        if (!tinta_vec_push(&state->nodes, &node) ||
            !tinta_map_set(&state->node_ids, spec->id.data, spec->id.len, *index)) {
            if (state->nodes.len == *index) node_destroy(&node);
            return set_error(error, "Out of memory");
        }
    }
    return true;
}

static bool set_class_style(ParseState *state, View name, const TintaMermaidStyle *style) {
    size_t index;
    if (tinta_map_get(&state->class_ids, name.data, name.len, &index)) {
        TINTA_VEC_AT(TintaMermaidClassStyle, state->class_styles, index).style = *style;
        return true;
    }
    {
        TintaMermaidClassStyle item;
        memset(&item, 0, sizeof(item));
        item.name = tinta_str8_dup(name.data, name.len);
        item.style = *style;
        if (!item.name) return false;
        index = state->class_styles.len;
        if (!tinta_vec_push(&state->class_styles, &item) ||
            !tinta_map_set(&state->class_ids, name.data, name.len, index)) {
            if (state->class_styles.len == index) free(item.name);
            return false;
        }
    }
    return true;
}

static size_t current_subgraph(const ParseState *state) {
    if (!state || !state->subgraph_stack.len) return SIZE_MAX;
    return TINTA_VEC_AT(size_t, state->subgraph_stack,
                        state->subgraph_stack.len - 1);
}

static bool rebuild_node_map(ParseState *state) {
    size_t i;
    tinta_map_destroy(&state->node_ids);
    if (!tinta_map_init(&state->node_ids)) return false;
    for (i = 0; i < state->nodes.len; i++) {
        TintaMermaidNode *node = TINTA_VEC_PTR(
            TintaMermaidNode, state->nodes, i);
        if (!tinta_map_set(&state->node_ids, node->id,
                           strlen(node->id), i)) return false;
    }
    return true;
}

static bool remove_placeholder_node(ParseState *state, size_t index) {
    TintaMermaidNode *nodes;
    if (!state || index >= state->nodes.len) return false;
    nodes = (TintaMermaidNode *)state->nodes.data;
    node_destroy(&nodes[index]);
    if (index + 1 < state->nodes.len)
        memmove(&nodes[index], &nodes[index + 1],
                (state->nodes.len - index - 1) * sizeof(*nodes));
    state->nodes.len--;
    return rebuild_node_map(state);
}

static bool parse_subgraph_declaration(ParseState *state, View value,
                                       size_t source_offset, char **error) {
    TintaMermaidSubgraph subgraph;
    TintaStr8 id = {0};
    TintaStr8 label = {0};
    size_t existing;
    size_t position = 0;
    View trimmed = trim(value);
    bool generated = false;
    memset(&subgraph, 0, sizeof(subgraph));
    if (!tinta_str8_init(&id) || !tinta_str8_init(&label))
        goto out_of_memory;
    if (!trimmed.len) {
        set_error(error, "Expected a title after subgraph");
        goto failed;
    }
    if (state->subgraph_stack.len >= 64) {
        set_error(error, "Mermaid subgraph nesting limit exceeded");
        goto failed;
    }
    if (trimmed.data[0] == '"' || trimmed.data[0] == '\'') {
        char quote = trimmed.data[0];
        size_t close = 1;
        bool escaped = false;
        while (close < trimmed.len) {
            char c = trimmed.data[close];
            if (c == quote && !escaped) break;
            escaped = c == '\\' && !escaped;
            if (c != '\\') escaped = false;
            close++;
        }
        if (close >= trimmed.len || trim(view_make(
                trimmed.data + close + 1,
                trimmed.len - close - 1)).len) {
            set_error(error, "Invalid quoted subgraph title");
            goto failed;
        }
        if (!decode_label(view_make(trimmed.data + 1, close - 1), &label))
            goto out_of_memory;
        generated = true;
    } else {
        View first;
        while (position < trimmed.len &&
               !is_space(trimmed.data[position]) &&
               trimmed.data[position] != '[') position++;
        first = view_make(trimmed.data, position);
        View remainder = trim(view_make(trimmed.data + position,
                                        trimmed.len - position));
        if (!first.len) {
            set_error(error, "Expected a subgraph identifier or title");
            goto failed;
        }
        if (remainder.len && remainder.data[0] == '[') {
            size_t close = find_char(remainder, ']', 1);
            View encoded;
            if (close == SIZE_MAX || trim(view_make(
                    remainder.data + close + 1,
                    remainder.len - close - 1)).len) {
                set_error(error, "Invalid subgraph title delimiter");
                goto failed;
            }
            encoded = trim(view_make(remainder.data + 1, close - 1));
            if (encoded.len >= 2 &&
                ((encoded.data[0] == '"' && encoded.data[encoded.len - 1] == '"') ||
                 (encoded.data[0] == '\'' && encoded.data[encoded.len - 1] == '\''))) {
                encoded.data++;
                encoded.len -= 2;
            }
            if (!tinta_str8_assign(&id, first.data, first.len) ||
                !decode_label(encoded, &label)) goto out_of_memory;
        } else if (remainder.len) {
            if (!decode_label(trimmed, &label)) goto out_of_memory;
            generated = true;
        } else {
            if (!tinta_str8_assign(&id, first.data, first.len) ||
                !decode_label(first, &label)) goto out_of_memory;
        }
    }
    if (generated) {
        char buffer[64];
        int length = snprintf(buffer, sizeof(buffer), "__subgraph_%zu",
                              state->subgraphs.len + 1);
        if (length <= 0 || !tinta_str8_assign(&id, buffer, (size_t)length))
            goto out_of_memory;
    }
    if (tinta_map_get(&state->subgraph_ids, id.data, id.len, &existing)) {
        set_error(error, "Duplicate Mermaid subgraph identifier");
        goto failed;
    }
    if (tinta_map_get(&state->node_ids, id.data, id.len, &existing)) {
        TintaMermaidNode *node = TINTA_VEC_PTR(
            TintaMermaidNode, state->nodes, existing);
        if (node->has_definition) {
            set_error(error,
                "A Mermaid identifier cannot name both a node and a subgraph");
            goto failed;
        }
        if (!remove_placeholder_node(state, existing)) goto out_of_memory;
    }
    if (state->max_nodes &&
        state->nodes.len + state->subgraphs.len >= state->max_nodes) {
        set_error(error, "Mermaid node limit exceeded");
        goto failed;
    }
    subgraph.id = tinta_str8_dup(id.data, id.len);
    subgraph.label = tinta_str8_dup(label.data, label.len);
    subgraph.class_name = tinta_str8_dup("", 0);
    subgraph.parent_subgraph = current_subgraph(state);
    subgraph.direction = subgraph.parent_subgraph == SIZE_MAX ?
        state->direction : TINTA_VEC_AT(TintaMermaidSubgraph,
            state->subgraphs, subgraph.parent_subgraph).direction;
    subgraph.source_offset = source_offset;
    subgraph.style.stroke_width = 1.0f;
    if (!subgraph.id || !subgraph.label || !subgraph.class_name)
        goto out_of_memory_subgraph;
    existing = state->subgraphs.len;
    if (!tinta_vec_push(&state->subgraphs, &subgraph) ||
        !tinta_map_set(&state->subgraph_ids, id.data, id.len, existing) ||
        !tinta_vec_push(&state->subgraph_stack, &existing)) {
        if (state->subgraphs.len == existing)
            subgraph_destroy(&subgraph);
        goto out_of_memory;
    }
    tinta_str8_destroy(&id);
    tinta_str8_destroy(&label);
    return true;

out_of_memory_subgraph:
    subgraph_destroy(&subgraph);
out_of_memory:
    set_error(error, "Out of memory");
failed:
    tinta_str8_destroy(&id);
    tinta_str8_destroy(&label);
    return false;
}

static bool add_raw_edge(ParseState *state, const NodeSpec *from,
                         const NodeSpec *to, const ArrowSpec *arrow,
                         char **error) {
    RawEdge edge;
    memset(&edge, 0, sizeof(edge));
    if (state->max_edges && state->raw_edges.len >= state->max_edges)
        return set_error(error, "Mermaid edge limit exceeded");
    edge.from_id = tinta_str8_dup(from->id.data, from->id.len);
    edge.to_id = tinta_str8_dup(to->id.data, to->id.len);
    edge.label = tinta_str8_dup(arrow->label.data ? arrow->label.data : "",
                                arrow->label.len);
    edge.directed = arrow->directed;
    edge.dashed = arrow->dashed;
    edge.stroke_scale = arrow->stroke_scale;
    if (!edge.from_id || !edge.to_id || !edge.label ||
        !tinta_vec_push(&state->raw_edges, &edge)) {
        raw_edge_destroy(&edge);
        return set_error(error, "Out of memory");
    }
    return true;
}

static bool add_style_assignment(ParseState *state, View id,
                                 const TintaMermaidStyle *style,
                                 char **error) {
    StyleAssignment assignment;
    memset(&assignment, 0, sizeof(assignment));
    assignment.id = tinta_str8_dup(id.data, id.len);
    assignment.style = *style;
    if (!assignment.id ||
        !tinta_vec_push(&state->style_assignments, &assignment)) {
        style_assignment_destroy(&assignment);
        return set_error(error, "Out of memory");
    }
    return true;
}

static bool add_class_assignment(ParseState *state, View id, View class_name,
                                 size_t source_offset, char **error) {
    ClassAssignment assignment;
    memset(&assignment, 0, sizeof(assignment));
    assignment.id = tinta_str8_dup(id.data, id.len);
    assignment.class_name = tinta_str8_dup(class_name.data, class_name.len);
    assignment.source_offset = source_offset;
    if (!assignment.id || !assignment.class_name ||
        !tinta_vec_push(&state->class_assignments, &assignment)) {
        class_assignment_destroy(&assignment);
        return set_error(error, "Out of memory");
    }
    return true;
}

static bool parse_direction(View value, TintaMermaidDirection *direction) {
    if (view_eq_text_icase(value, "TB") || view_eq_text_icase(value, "TD"))
        *direction = TINTA_MERMAID_TOP_TO_BOTTOM;
    else if (view_eq_text_icase(value, "BT")) *direction = TINTA_MERMAID_BOTTOM_TO_TOP;
    else if (view_eq_text_icase(value, "LR")) *direction = TINTA_MERMAID_LEFT_TO_RIGHT;
    else if (view_eq_text_icase(value, "RL")) *direction = TINTA_MERMAID_RIGHT_TO_LEFT;
    else return false;
    return true;
}

static bool ensure_named_placeholder(ParseState *state, const char *id,
                                     size_t source_offset, char **error) {
    NodeSpec spec;
    size_t index;
    if (!node_spec_init(&spec)) return set_error(error, "Out of memory");
    if (!tinta_str8_assign(&spec.id, id, strlen(id)) ||
        !tinta_str8_assign(&spec.label, id, strlen(id))) {
        node_spec_destroy(&spec);
        return set_error(error, "Out of memory");
    }
    spec.source_offset = source_offset;
    if (!ensure_node(state, &spec, &index, error)) {
        node_spec_destroy(&spec);
        return false;
    }
    node_spec_destroy(&spec);
    return true;
}

static bool resolve_symbol(ParseState *state, const char *id,
                           bool *subgraph, size_t *index) {
    size_t value;
    if (tinta_map_get(&state->subgraph_ids, id, strlen(id), &value)) {
        *subgraph = true;
        *index = value;
        return true;
    }
    if (tinta_map_get(&state->node_ids, id, strlen(id), &value)) {
        *subgraph = false;
        *index = value;
        return true;
    }
    return false;
}

static bool finalize_assignments(ParseState *state, char **error) {
    size_t i;
    for (i = 0; i < state->style_assignments.len; i++) {
        StyleAssignment *assignment = TINTA_VEC_PTR(
            StyleAssignment, state->style_assignments, i);
        bool is_subgraph;
        size_t index;
        if (!resolve_symbol(state, assignment->id, &is_subgraph, &index)) {
            if (!ensure_named_placeholder(state, assignment->id, 0, error) ||
                !resolve_symbol(state, assignment->id, &is_subgraph, &index))
                return false;
        }
        if (is_subgraph)
            merge_style(&TINTA_VEC_AT(TintaMermaidSubgraph,
                state->subgraphs, index).style, &assignment->style);
        else
            merge_style(&TINTA_VEC_AT(TintaMermaidNode,
                state->nodes, index).style, &assignment->style);
    }
    for (i = 0; i < state->class_assignments.len; i++) {
        ClassAssignment *assignment = TINTA_VEC_PTR(
            ClassAssignment, state->class_assignments, i);
        bool is_subgraph;
        size_t index;
        char **target;
        char *copy;
        if (!resolve_symbol(state, assignment->id, &is_subgraph, &index)) {
            if (!ensure_named_placeholder(state, assignment->id,
                    assignment->source_offset, error) ||
                !resolve_symbol(state, assignment->id,
                                &is_subgraph, &index)) return false;
        }
        target = is_subgraph ?
            &TINTA_VEC_AT(TintaMermaidSubgraph,
                state->subgraphs, index).class_name :
            &TINTA_VEC_AT(TintaMermaidNode,
                state->nodes, index).class_name;
        copy = tinta_str8_dup(assignment->class_name,
                              strlen(assignment->class_name));
        if (!copy) return set_error(error, "Out of memory");
        free(*target);
        *target = copy;
    }
    return true;
}

static bool finalize_edges(ParseState *state, char **error) {
    size_t i;
    for (i = 0; i < state->raw_edges.len; i++) {
        RawEdge *raw = TINTA_VEC_PTR(RawEdge, state->raw_edges, i);
        TintaMermaidEdge edge;
        memset(&edge, 0, sizeof(edge));
        if (!resolve_symbol(state, raw->from_id,
                            &edge.from_subgraph, &edge.from) ||
            !resolve_symbol(state, raw->to_id,
                            &edge.to_subgraph, &edge.to))
            return set_error(error, "Unknown Mermaid edge endpoint");
        edge.label = tinta_str8_dup(raw->label, strlen(raw->label));
        edge.directed = raw->directed;
        edge.dashed = raw->dashed;
        edge.stroke_scale = raw->stroke_scale;
        if (!edge.label || !tinta_vec_push(&state->edges, &edge)) {
            edge_destroy(&edge);
            return set_error(error, "Out of memory");
        }
    }
    return true;
}

static void normalize_statements(char *text, size_t length) {
    char quote = 0;
    bool escaped = false;
    bool comment = false;
    bool pipe_label = false;
    int square_depth = 0, round_depth = 0, curly_depth = 0;
    size_t i;
    if (length >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text[0] = text[1] = text[2] = ' ';
    for (i = 0; i < length; i++) {
        char c = text[i];
        if (c == '\n') { comment = false; pipe_label = false; continue; }
        if (comment) continue;
        if (quote) {
            if (c == quote && !escaped) quote = 0;
            escaped = c == '\\' && !escaped;
            if (c != '\\') escaped = false;
            continue;
        }
        if (pipe_label) { if (c == '|') pipe_label = false; continue; }
        if (c == '"' || c == '\'') { quote = c; escaped = false; continue; }
        if (c == '%' && i + 1 < length && text[i + 1] == '%' &&
            !square_depth && !round_depth && !curly_depth) { comment = true; continue; }
        if (c == '|' && !square_depth && !round_depth && !curly_depth) pipe_label = true;
        else if (c == '[') square_depth++;
        else if (c == ']' && square_depth) square_depth--;
        else if (c == '(') round_depth++;
        else if (c == ')' && round_depth) round_depth--;
        else if (c == '{') curly_depth++;
        else if (c == '}' && curly_depth) curly_depth--;
        else if (c == ';' && !square_depth && !round_depth && !curly_depth) text[i] = '\n';
    }
}

static TintaMermaidParseResult fail_result(ParseState *state, char *normalized,
                                           size_t line, char *error) {
    TintaMermaidParseResult result;
    memset(&result, 0, sizeof(result));
    result.error_line = line;
    result.error = error ? error : tinta_str8_dup("Out of memory", 13);
    parse_state_destroy(state);
    free(normalized);
    return result;
}

TintaMermaidParseResult tinta_mermaid_parse_limited(
    const char *source, size_t length, size_t max_nodes, size_t max_edges) {
    TintaMermaidParseResult result;
    ParseState state;
    char *normalized;
    bool found_header = false;
    size_t line_number = 0, line_offset = 0;
    char *error = NULL;
    memset(&result, 0, sizeof(result));
    if (!source && length) {
        result.error = tinta_str8_dup("Invalid source", 14);
        return result;
    }
    if (!parse_state_init(&state)) {
        parse_state_destroy(&state);
        result.error = tinta_str8_dup("Out of memory", 13);
        return result;
    }
    state.max_nodes = max_nodes;
    state.max_edges = max_edges;
    normalized = tinta_str8_dup(source ? source : "", length);
    if (!normalized) return fail_result(&state, NULL, 0, NULL);
    normalize_statements(normalized, length);
    while (line_offset <= length) {
        size_t line_end = line_offset;
        View line, keyword;
        size_t position = 0;
        line_number++;
        while (line_end < length && normalized[line_end] != '\n') line_end++;
        line = view_make(normalized + line_offset, line_end - line_offset);
        if (line.len && line.data[line.len - 1] == '\r') line.len--;
        line = trim(line);
        if (line.len && line.data[line.len - 1] == ';') { line.len--; line = trim(line); }
        if (line.len && !starts_at(line, 0, "%%")) {
            keyword = read_word(line, &position);
            if (!found_header) {
                View direction;
                if (!view_eq_text_icase(keyword, "flowchart") && !view_eq_text_icase(keyword, "graph")) {
                    set_error(&error, "Only Mermaid flowchart and graph diagrams are supported");
                    return fail_result(&state, normalized, line_number, error);
                }
                direction = read_word(line, &position);
                if (direction.len && !parse_direction(direction, &state.direction)) {
                    set_error(&error, "Unsupported flowchart direction");
                    return fail_result(&state, normalized, line_number, error);
                }
                found_header = true;
            } else if (view_eq_text_icase(keyword, "classDef")) {
                View class_name = read_word(line, &position);
                TintaMermaidStyle style;
                memset(&style, 0, sizeof(style)); style.stroke_width = 1.0f;
                if (!class_name.len) {
                    set_error(&error, "Expected a class name after classDef");
                    return fail_result(&state, normalized, line_number, error);
                }
                if (!parse_style_list(trim(view_make(line.data + position, line.len - position)), &style, &error) ||
                    !set_class_style(&state, class_name, &style))
                    return fail_result(&state, normalized, line_number, error);
            } else if (view_eq_text_icase(keyword, "class")) {
                View ids = read_word(line, &position);
                View class_name = read_word(line, &position);
                size_t id_position = 0;
                if (!ids.len || !class_name.len) {
                    set_error(&error, "Expected node IDs and a class name");
                    return fail_result(&state, normalized, line_number, error);
                }
                while (id_position <= ids.len) {
                    size_t comma = find_char(ids, ',', id_position);
                    View id;
                    if (comma == SIZE_MAX) comma = ids.len;
                    id = trim(view_make(ids.data + id_position, comma - id_position));
                    if (id.len && !add_class_assignment(
                            &state, id, class_name, line_offset, &error))
                        return fail_result(&state, normalized,
                                           line_number, error);
                    if (comma == ids.len) break;
                    id_position = comma + 1;
                }
            } else if (view_eq_text_icase(keyword, "style")) {
                View id = read_word(line, &position);
                TintaMermaidStyle style;
                memset(&style, 0, sizeof(style)); style.stroke_width = 1.0f;
                if (!id.len) {
                    set_error(&error, "Expected a node ID after style");
                    return fail_result(&state, normalized, line_number, error);
                }
                if (!parse_style_list(trim(view_make(line.data + position, line.len - position)), &style, &error))
                    return fail_result(&state, normalized, line_number, error);
                if (!add_style_assignment(&state, id, &style, &error))
                    return fail_result(&state, normalized, line_number, error);
            } else if (view_eq_text_icase(keyword, "subgraph")) {
                if (!parse_subgraph_declaration(
                        &state, view_make(line.data + position,
                                         line.len - position),
                        line_offset, &error))
                    return fail_result(&state, normalized,
                                       line_number, error);
            } else if (view_eq_text_icase(keyword, "end")) {
                if (!state.subgraph_stack.len ||
                    trim(view_make(line.data + position,
                                   line.len - position)).len) {
                    set_error(&error, "Unexpected Mermaid subgraph end");
                    return fail_result(&state, normalized,
                                       line_number, error);
                }
                state.subgraph_stack.len--;
            } else if (view_eq_text_icase(keyword, "direction")) {
                TintaMermaidDirection direction;
                size_t parent = current_subgraph(&state);
                View value = trim(view_make(line.data + position,
                                            line.len - position));
                if (parent == SIZE_MAX || !parse_direction(value, &direction)) {
                    set_error(&error, "Invalid subgraph direction");
                    return fail_result(&state, normalized,
                                       line_number, error);
                }
                TINTA_VEC_AT(TintaMermaidSubgraph,
                             state.subgraphs, parent).direction = direction;
                TINTA_VEC_AT(TintaMermaidSubgraph,
                             state.subgraphs, parent).has_direction = true;
            } else if (view_eq_text_icase(keyword, "click") ||
                       view_eq_text_icase(keyword, "linkStyle")) {
                set_error(&error, "This Mermaid flowchart statement is not supported");
                return fail_result(&state, normalized, line_number, error);
            } else {
                NodeSpec current;
                size_t current_node;
                position = 0;
                node_spec_init(&current);
                current.parent_subgraph = current_subgraph(&state);
                if (!parse_node_spec(line, &position, line_offset, &current, &error) ||
                    !ensure_node(&state, &current, &current_node, &error)) {
                    node_spec_destroy(&current);
                    return fail_result(&state, normalized, line_number, error);
                }
                for (;;) {
                    ArrowSpec arrow;
                    NodeSpec next;
                    size_t next_node;
                    skip_spaces(line, &position);
                    if (position >= line.len) break;
                    arrow_spec_init(&arrow);
                    node_spec_init(&next);
                    next.parent_subgraph = current_subgraph(&state);
                    if (!parse_arrow(line, &position, &arrow, &error) ||
                        !parse_node_spec(line, &position, line_offset, &next, &error) ||
                        !ensure_node(&state, &next, &next_node, &error) ||
                        !add_raw_edge(&state, &current, &next,
                                      &arrow, &error)) {
                        arrow_spec_destroy(&arrow);
                        node_spec_destroy(&next);
                        node_spec_destroy(&current);
                        return fail_result(&state, normalized, line_number, error);
                    }
                    arrow_spec_destroy(&arrow);
                    node_spec_destroy(&current);
                    current = next;
                    memset(&next, 0, sizeof(next));
                    current_node = next_node;
                }
                node_spec_destroy(&current);
            }
        }
        if (line_end == length) break;
        line_offset = line_end + 1;
    }
    if (!found_header) {
        set_error(&error, "Only Mermaid flowchart and graph diagrams are supported");
        return fail_result(&state, normalized, 0, error);
    }
    if (state.subgraph_stack.len) {
        set_error(&error, "Unterminated Mermaid subgraph");
        return fail_result(&state, normalized, line_number, error);
    }
    if (!finalize_assignments(&state, &error) ||
        !finalize_edges(&state, &error))
        return fail_result(&state, normalized, line_number, error);
    if (!state.nodes.len && !state.subgraphs.len) {
        set_error(&error, "The Mermaid flowchart has no nodes or subgraphs");
        return fail_result(&state, normalized, 0, error);
    }
    result.diagram.direction = state.direction;
    result.diagram.nodes = (TintaMermaidNode *)state.nodes.data;
    result.diagram.node_count = state.nodes.len;
    result.diagram.edges = (TintaMermaidEdge *)state.edges.data;
    result.diagram.edge_count = state.edges.len;
    result.diagram.subgraphs = (TintaMermaidSubgraph *)state.subgraphs.data;
    result.diagram.subgraph_count = state.subgraphs.len;
    result.diagram.class_styles = (TintaMermaidClassStyle *)state.class_styles.data;
    result.diagram.class_style_count = state.class_styles.len;
    state.nodes.data = state.edges.data = state.subgraphs.data =
        state.class_styles.data = NULL;
    state.nodes.len = state.edges.len = state.subgraphs.len =
        state.class_styles.len = 0;
    parse_state_destroy(&state);
    free(normalized);
    result.success = true;
    return result;
}

TintaMermaidParseResult tinta_mermaid_parse(const char *source, size_t length) {
    return tinta_mermaid_parse_limited(source, length, 0, 0);
}

void tinta_mermaid_parse_result_destroy(TintaMermaidParseResult *result) {
    size_t i;
    if (!result) return;
    for (i = 0; i < result->diagram.node_count; i++) node_destroy(&result->diagram.nodes[i]);
    for (i = 0; i < result->diagram.edge_count; i++) edge_destroy(&result->diagram.edges[i]);
    for (i = 0; i < result->diagram.subgraph_count; i++)
        subgraph_destroy(&result->diagram.subgraphs[i]);
    for (i = 0; i < result->diagram.class_style_count; i++) class_style_destroy(&result->diagram.class_styles[i]);
    free(result->diagram.nodes);
    free(result->diagram.edges);
    free(result->diagram.subgraphs);
    free(result->diagram.class_styles);
    free(result->error);
    memset(result, 0, sizeof(*result));
}

const TintaMermaidSubgraph *tinta_mermaid_find_subgraph(
        const TintaMermaidDiagram *diagram, const char *id) {
    size_t i;
    if (!diagram || !id) return NULL;
    for (i = 0; i < diagram->subgraph_count; i++)
        if (strcmp(diagram->subgraphs[i].id, id) == 0)
            return &diagram->subgraphs[i];
    return NULL;
}

const TintaMermaidNode *tinta_mermaid_find_node(const TintaMermaidDiagram *diagram,
                                                const char *id) {
    size_t i;
    if (!diagram || !id) return NULL;
    for (i = 0; i < diagram->node_count; i++) if (strcmp(diagram->nodes[i].id, id) == 0) return &diagram->nodes[i];
    return NULL;
}

const TintaMermaidStyle *tinta_mermaid_find_class_style(const TintaMermaidDiagram *diagram,
                                                        const char *name) {
    size_t i;
    if (!diagram || !name) return NULL;
    for (i = 0; i < diagram->class_style_count; i++)
        if (strcmp(diagram->class_styles[i].name, name) == 0) return &diagram->class_styles[i].style;
    return NULL;
}

static void index_lists_destroy(IndexList *lists, size_t count) {
    size_t i;
    if (!lists) return;
    for (i = 0; i < count; i++) tinta_vec_destroy(&lists[i].values);
    free(lists);
}

static float barycenter(size_t node, const IndexList *incoming, const size_t *rank,
                        const float *order, size_t level) {
    const TintaVec *values = &incoming[node].values;
    float total = 0.0f;
    size_t count = 0, i;
    if (!values->len) return (float)node;
    for (i = 0; i < values->len; i++) {
        size_t parent = TINTA_VEC_AT(size_t, *values, i);
        if (rank[parent] < level) { total += order[parent]; count++; }
    }
    return count ? total / (float)count : (float)node;
}

typedef struct FlatLayout {
    TintaMermaidRect *items;
    size_t *ranks;
    size_t count;
    float width;
    float height;
} FlatLayout;

typedef struct ScopeItem {
    bool subgraph;
    size_t index;
    TintaMermaidSize size;
} ScopeItem;

typedef struct ScopeProjection {
    bool from_subgraph;
    bool to_subgraph;
    size_t from;
    size_t to;
} ScopeProjection;

typedef struct ScopeLists {
    TintaVec nodes;
    TintaVec subgraphs;
    TintaVec projections;
} ScopeLists;

typedef struct HierarchyLayoutContext {
    const TintaMermaidDiagram *diagram;
    const TintaMermaidSize *node_sizes;
    const TintaMermaidSize *title_sizes;
    float scale_factor;
    float node_gap;
    float rank_gap;
    TintaMermaidLayout *result;
    ScopeLists *scopes;
    size_t scope_count;
} HierarchyLayoutContext;

static void flat_layout_destroy(FlatLayout *layout) {
    if (!layout) return;
    free(layout->items);
    free(layout->ranks);
    memset(layout, 0, sizeof(*layout));
}

static FlatLayout layout_flat(TintaMermaidDirection direction,
                              const TintaMermaidSize *sizes, size_t n,
                              const LayoutEdge *edges, size_t edge_count,
                              float node_gap, float rank_gap) {
    FlatLayout result;
    IndexList *outgoing = NULL, *incoming = NULL, *levels = NULL;
    size_t *indegree = NULL, *rank = NULL, *queue = NULL;
    bool *processed = NULL;
    float *order = NULL, *rank_widths = NULL, *rank_heights = NULL;
    size_t i, max_rank = 0, queue_head = 0, queue_tail = 0, next_rank;
    bool processed_any = false;
    memset(&result, 0, sizeof(result));
    if (!n) return result;
    node_gap = maxf(0.0f, node_gap);
    rank_gap = maxf(0.0f, rank_gap);
    outgoing = (IndexList *)calloc(n, sizeof(*outgoing));
    incoming = (IndexList *)calloc(n, sizeof(*incoming));
    indegree = (size_t *)calloc(n, sizeof(*indegree));
    rank = (size_t *)calloc(n, sizeof(*rank));
    queue = (size_t *)malloc(n * sizeof(*queue));
    processed = (bool *)calloc(n, sizeof(*processed));
    order = (float *)calloc(n, sizeof(*order));
    if (!outgoing || !incoming || !indegree || !rank || !queue || !processed || !order) goto cleanup;
    for (i = 0; i < n; i++) {
        tinta_vec_init(&outgoing[i].values, sizeof(size_t));
        tinta_vec_init(&incoming[i].values, sizeof(size_t));
    }
    for (i = 0; i < edge_count; i++) {
        const LayoutEdge *edge = &edges[i];
        if (edge->from >= n || edge->to >= n) continue;
        if (!tinta_vec_push(&outgoing[edge->from].values, &edge->to) ||
            !tinta_vec_push(&incoming[edge->to].values, &edge->from)) goto cleanup;
        indegree[edge->to]++;
    }
    for (i = 0; i < n; i++) if (!indegree[i]) queue[queue_tail++] = i;
    while (queue_head < queue_tail) {
        size_t node = queue[queue_head++], j;
        processed[node] = true;
        processed_any = true;
        if (rank[node] > max_rank) max_rank = rank[node];
        for (j = 0; j < outgoing[node].values.len; j++) {
            size_t target = TINTA_VEC_AT(size_t, outgoing[node].values, j);
            if (rank[target] < rank[node] + 1) rank[target] = rank[node] + 1;
            if (--indegree[target] == 0) queue[queue_tail++] = target;
        }
    }
    next_rank = processed_any ? max_rank + 1 : 0;
    for (i = 0; i < n; i++) if (!processed[i]) rank[i] = next_rank++;
    for (i = 0; i < n; i++) if (rank[i] > max_rank) max_rank = rank[i];
    levels = (IndexList *)calloc(max_rank + 1, sizeof(*levels));
    rank_widths = (float *)calloc(max_rank + 1, sizeof(*rank_widths));
    rank_heights = (float *)calloc(max_rank + 1, sizeof(*rank_heights));
    result.items = (TintaMermaidRect *)calloc(n, sizeof(*result.items));
    result.ranks = (size_t *)malloc(n * sizeof(*result.ranks));
    if (!levels || !rank_widths || !rank_heights || !result.items || !result.ranks) goto cleanup;
    for (i = 0; i <= max_rank; i++) tinta_vec_init(&levels[i].values, sizeof(size_t));
    for (i = 0; i < n; i++) if (!tinta_vec_push(&levels[rank[i]].values, &i)) goto cleanup;
    memcpy(result.ranks, rank, n * sizeof(*rank));
    result.count = n;
    for (i = 0; i <= max_rank; i++) {
        TintaVec *nodes = &levels[i].values;
        size_t j;
        if (i > 0) {
            for (j = 1; j < nodes->len; j++) {
                size_t value = TINTA_VEC_AT(size_t, *nodes, j), k = j;
                float value_bary = barycenter(value, incoming, rank, order, i);
                while (k > 0) {
                    size_t previous = TINTA_VEC_AT(size_t, *nodes, k - 1);
                    if (barycenter(previous, incoming, rank, order, i) <= value_bary) break;
                    TINTA_VEC_AT(size_t, *nodes, k) = previous;
                    k--;
                }
                TINTA_VEC_AT(size_t, *nodes, k) = value;
            }
        }
        for (j = 0; j < nodes->len; j++) order[TINTA_VEC_AT(size_t, *nodes, j)] = (float)j;
    }
    if (direction == TINTA_MERMAID_TOP_TO_BOTTOM || direction == TINTA_MERMAID_BOTTOM_TO_TOP) {
        float y = 0.0f;
        for (i = 0; i <= max_rank; i++) {
            size_t j;
            for (j = 0; j < levels[i].values.len; j++) {
                size_t node = TINTA_VEC_AT(size_t, levels[i].values, j);
                rank_widths[i] += maxf(1.0f, sizes[node].width);
                rank_heights[i] = maxf(rank_heights[i], maxf(1.0f, sizes[node].height));
            }
            if (levels[i].values.len > 1) rank_widths[i] += node_gap * (float)(levels[i].values.len - 1);
            result.width = maxf(result.width, rank_widths[i]);
        }
        for (i = 0; i <= max_rank; i++) {
            float x = (result.width - rank_widths[i]) * 0.5f;
            size_t j;
            for (j = 0; j < levels[i].values.len; j++) {
                size_t node = TINTA_VEC_AT(size_t, levels[i].values, j);
                float width = maxf(1.0f, sizes[node].width);
                float height = maxf(1.0f, sizes[node].height);
                float node_y = y + (rank_heights[i] - height) * 0.5f;
                result.items[node].left = x; result.items[node].top = node_y;
                result.items[node].right = x + width; result.items[node].bottom = node_y + height;
                x += width + node_gap;
            }
            y += rank_heights[i];
            if (i < max_rank) y += rank_gap;
        }
        result.height = y;
        if (direction == TINTA_MERMAID_BOTTOM_TO_TOP) {
            for (i = 0; i < n; i++) {
                float old_top = result.items[i].top;
                result.items[i].top = result.height - result.items[i].bottom;
                result.items[i].bottom = result.height - old_top;
            }
        }
    } else {
        float x = 0.0f;
        for (i = 0; i <= max_rank; i++) {
            size_t j;
            for (j = 0; j < levels[i].values.len; j++) {
                size_t node = TINTA_VEC_AT(size_t, levels[i].values, j);
                rank_widths[i] = maxf(rank_widths[i], maxf(1.0f, sizes[node].width));
                rank_heights[i] += maxf(1.0f, sizes[node].height);
            }
            if (levels[i].values.len > 1) rank_heights[i] += node_gap * (float)(levels[i].values.len - 1);
            result.height = maxf(result.height, rank_heights[i]);
        }
        for (i = 0; i <= max_rank; i++) {
            float y = (result.height - rank_heights[i]) * 0.5f;
            size_t j;
            for (j = 0; j < levels[i].values.len; j++) {
                size_t node = TINTA_VEC_AT(size_t, levels[i].values, j);
                float width = maxf(1.0f, sizes[node].width);
                float height = maxf(1.0f, sizes[node].height);
                float node_x = x + (rank_widths[i] - width) * 0.5f;
                result.items[node].left = node_x; result.items[node].top = y;
                result.items[node].right = node_x + width; result.items[node].bottom = y + height;
                y += height + node_gap;
            }
            x += rank_widths[i];
            if (i < max_rank) x += rank_gap;
        }
        result.width = x;
        if (direction == TINTA_MERMAID_RIGHT_TO_LEFT) {
            for (i = 0; i < n; i++) {
                float old_left = result.items[i].left;
                result.items[i].left = result.width - result.items[i].right;
                result.items[i].right = result.width - old_left;
            }
        }
    }

cleanup:
    index_lists_destroy(outgoing, outgoing ? n : 0);
    index_lists_destroy(incoming, incoming ? n : 0);
    index_lists_destroy(levels, levels ? max_rank + 1 : 0);
    free(indegree); free(rank); free(queue); free(processed); free(order);
    free(rank_widths); free(rank_heights);
    if (result.count != n) flat_layout_destroy(&result);
    return result;
}

static bool endpoint_direct_child(const TintaMermaidDiagram *diagram,
                                  size_t scope, bool endpoint_subgraph,
                                  size_t endpoint, bool *child_subgraph,
                                  size_t *child_index) {
    size_t group;
    if (endpoint_subgraph) {
        if (endpoint >= diagram->subgraph_count || endpoint == scope)
            return false;
        group = endpoint;
    } else {
        if (endpoint >= diagram->node_count) return false;
        group = diagram->nodes[endpoint].parent_subgraph;
        if (group == scope) {
            *child_subgraph = false;
            *child_index = endpoint;
            return true;
        }
        if (group == SIZE_MAX) return false;
    }
    while (group < diagram->subgraph_count &&
           diagram->subgraphs[group].parent_subgraph != scope) {
        group = diagram->subgraphs[group].parent_subgraph;
        if (group == SIZE_MAX) return false;
    }
    if (group >= diagram->subgraph_count) return false;
    *child_subgraph = true;
    *child_index = group;
    return true;
}

static size_t scope_item_index(const TintaVec *items, bool subgraph,
                               size_t index) {
    size_t i;
    for (i = 0; i < items->len; i++) {
        const ScopeItem *item = TINTA_VEC_PTR(ScopeItem, *items, i);
        if (item->subgraph == subgraph && item->index == index) return i;
    }
    return SIZE_MAX;
}

static size_t scope_slot(const HierarchyLayoutContext *context,
                         size_t scope) {
    return scope == SIZE_MAX ? context->diagram->subgraph_count : scope;
}

static void scope_lists_destroy(HierarchyLayoutContext *context) {
    size_t i;
    if (!context || !context->scopes) return;
    for (i = 0; i < context->scope_count; i++) {
        tinta_vec_destroy(&context->scopes[i].nodes);
        tinta_vec_destroy(&context->scopes[i].subgraphs);
        tinta_vec_destroy(&context->scopes[i].projections);
    }
    free(context->scopes);
    context->scopes = NULL;
    context->scope_count = 0;
}

static bool scope_lists_build(HierarchyLayoutContext *context) {
    const TintaMermaidDiagram *diagram = context->diagram;
    size_t i;
    context->scope_count = diagram->subgraph_count + 1;
    context->scopes = (ScopeLists *)calloc(
        context->scope_count, sizeof(*context->scopes));
    if (!context->scopes) return false;
    for (i = 0; i < context->scope_count; i++) {
        if (!tinta_vec_init(&context->scopes[i].nodes, sizeof(size_t)) ||
            !tinta_vec_init(&context->scopes[i].subgraphs, sizeof(size_t)) ||
            !tinta_vec_init(&context->scopes[i].projections,
                            sizeof(ScopeProjection))) return false;
    }
    for (i = 0; i < diagram->node_count; i++) {
        size_t slot = diagram->nodes[i].parent_subgraph == SIZE_MAX ?
            diagram->subgraph_count : diagram->nodes[i].parent_subgraph;
        if (slot >= context->scope_count ||
            !tinta_vec_push(&context->scopes[slot].nodes, &i)) return false;
    }
    for (i = 0; i < diagram->subgraph_count; i++) {
        size_t slot = diagram->subgraphs[i].parent_subgraph == SIZE_MAX ?
            diagram->subgraph_count : diagram->subgraphs[i].parent_subgraph;
        if (slot >= context->scope_count ||
            !tinta_vec_push(&context->scopes[slot].subgraphs, &i)) return false;
    }
    for (i = 0; i < diagram->edge_count; i++) {
        const TintaMermaidEdge *edge = &diagram->edges[i];
        size_t scope = SIZE_MAX;
        size_t depth;
        for (depth = 0; depth <= 64; depth++) {
            ScopeProjection projection;
            if (!endpoint_direct_child(diagram, scope,
                    edge->from_subgraph, edge->from,
                    &projection.from_subgraph, &projection.from) ||
                !endpoint_direct_child(diagram, scope,
                    edge->to_subgraph, edge->to,
                    &projection.to_subgraph, &projection.to)) break;
            if (projection.from_subgraph == projection.to_subgraph &&
                projection.from == projection.to) {
                if (!projection.from_subgraph) break;
                scope = projection.from;
                continue;
            }
            if (!tinta_vec_push(
                    &context->scopes[scope_slot(context, scope)].projections,
                    &projection)) return false;
            break;
        }
    }
    return true;
}

static void translate_subgraph_tree(HierarchyLayoutContext *context,
                                    size_t group, float dx, float dy) {
    const ScopeLists *children =
        &context->scopes[scope_slot(context, group)];
    size_t i;
    TintaMermaidRect *rect = &context->result->subgraphs[group];
    rect->left += dx; rect->right += dx;
    rect->top += dy; rect->bottom += dy;
    for (i = 0; i < children->nodes.len; i++) {
        size_t node_index = TINTA_VEC_AT(size_t, children->nodes, i);
        TintaMermaidRect *node = &context->result->nodes[node_index];
        node->left += dx; node->right += dx;
        node->top += dy; node->bottom += dy;
    }
    for (i = 0; i < children->subgraphs.len; i++)
        translate_subgraph_tree(context,
            TINTA_VEC_AT(size_t, children->subgraphs, i), dx, dy);
}

static bool layout_scope(HierarchyLayoutContext *context, size_t scope,
                         TintaMermaidDirection inherited,
                         float *out_width, float *out_height) {
    const TintaMermaidDiagram *diagram = context->diagram;
    const ScopeLists *scope_lists =
        &context->scopes[scope_slot(context, scope)];
    TintaVec items = {0};
    TintaVec edges = {0};
    TintaMermaidDirection direction = inherited;
    FlatLayout flat;
    size_t i;
    float width;
    float height;
    float offset_x = 0;
    float offset_y = 0;
    bool ok = false;
    memset(&flat, 0, sizeof(flat));
    if (!tinta_vec_init(&items, sizeof(ScopeItem)) ||
        !tinta_vec_init(&edges, sizeof(LayoutEdge))) goto cleanup;
    if (scope != SIZE_MAX) {
        const TintaMermaidSubgraph *group = &diagram->subgraphs[scope];
        if (group->has_direction) direction = group->direction;
    }
    for (i = 0; i < scope_lists->nodes.len; i++) {
        ScopeItem item;
        item.subgraph = false;
        item.index = TINTA_VEC_AT(size_t, scope_lists->nodes, i);
        item.size = context->node_sizes[item.index];
        if (!tinta_vec_push(&items, &item)) goto cleanup;
    }
    for (i = 0; i < scope_lists->subgraphs.len; i++) {
        ScopeItem item;
        item.subgraph = true;
        item.index = TINTA_VEC_AT(size_t, scope_lists->subgraphs, i);
        if (!layout_scope(context, item.index, direction,
                          &item.size.width, &item.size.height) ||
            !tinta_vec_push(&items, &item)) goto cleanup;
    }
    for (i = 0; i < scope_lists->projections.len; i++) {
        const ScopeProjection *source = TINTA_VEC_PTR(
            ScopeProjection, scope_lists->projections, i);
        LayoutEdge edge;
        edge.from = scope_item_index(&items,
            source->from_subgraph, source->from);
        edge.to = scope_item_index(&items,
            source->to_subgraph, source->to);
        if (edge.from == SIZE_MAX || edge.to == SIZE_MAX ||
            edge.from == edge.to) continue;
        if (!tinta_vec_push(&edges, &edge)) goto cleanup;
    }
    if (items.len) {
        TintaMermaidSize *sizes = (TintaMermaidSize *)calloc(
            items.len, sizeof(*sizes));
        if (!sizes) goto cleanup;
        for (i = 0; i < items.len; i++)
            sizes[i] = TINTA_VEC_AT(ScopeItem, items, i).size;
        flat = layout_flat(direction, sizes, items.len,
                           (const LayoutEdge *)edges.data, edges.len,
                           context->node_gap, context->rank_gap);
        free(sizes);
        if (flat.count != items.len) goto cleanup;
    }
    width = flat.width;
    height = flat.height;
    if (scope != SIZE_MAX) {
        const TintaMermaidSize title = context->title_sizes[scope];
        float title_area = maxf(36.0f * context->scale_factor,
                                title.height + 12.0f * context->scale_factor);
        width = maxf(160.0f * context->scale_factor,
            maxf(width + 40.0f * context->scale_factor,
                 title.width + 40.0f * context->scale_factor));
        height = maxf(84.0f * context->scale_factor,
                      height + title_area + 20.0f * context->scale_factor);
        offset_x = (width - flat.width) * 0.5f;
        offset_y = title_area;
        context->result->subgraphs[scope] =
            (TintaMermaidRect){0, 0, width, height};
    }
    for (i = 0; i < items.len; i++) {
        const ScopeItem *item = TINTA_VEC_PTR(ScopeItem, items, i);
        const TintaMermaidRect *position = &flat.items[i];
        float dx = position->left + offset_x;
        float dy = position->top + offset_y;
        if (item->subgraph)
            translate_subgraph_tree(context, item->index, dx, dy);
        else {
            TintaMermaidRect *node = &context->result->nodes[item->index];
            node->left = dx;
            node->top = dy;
            node->right = dx + item->size.width;
            node->bottom = dy + item->size.height;
            context->result->ranks[item->index] = flat.ranks ? flat.ranks[i] : 0;
        }
    }
    *out_width = width;
    *out_height = height;
    ok = true;
cleanup:
    flat_layout_destroy(&flat);
    tinta_vec_destroy(&items);
    tinta_vec_destroy(&edges);
    return ok;
}

TintaMermaidLayout tinta_mermaid_layout(
        const TintaMermaidDiagram *diagram,
        const TintaMermaidSize *node_sizes, size_t node_size_count,
        const TintaMermaidSize *subgraph_title_sizes,
        size_t subgraph_title_size_count,
        float scale_factor,
        float node_gap, float rank_gap) {
    TintaMermaidLayout result;
    HierarchyLayoutContext context;
    memset(&result, 0, sizeof(result));
    if (!diagram || node_size_count != diagram->node_count ||
        subgraph_title_size_count != diagram->subgraph_count ||
        (diagram->node_count && !node_sizes) ||
        (diagram->subgraph_count && !subgraph_title_sizes)) return result;
    result.nodes = (TintaMermaidRect *)calloc(
        diagram->node_count ? diagram->node_count : 1, sizeof(*result.nodes));
    result.subgraphs = (TintaMermaidRect *)calloc(
        diagram->subgraph_count ? diagram->subgraph_count : 1,
        sizeof(*result.subgraphs));
    result.ranks = (size_t *)calloc(
        diagram->node_count ? diagram->node_count : 1,
        sizeof(*result.ranks));
    if (!result.nodes || !result.subgraphs || !result.ranks) {
        tinta_mermaid_layout_destroy(&result);
        return result;
    }
    result.node_count = result.rank_count = diagram->node_count;
    result.subgraph_count = diagram->subgraph_count;
    memset(&context, 0, sizeof(context));
    context.diagram = diagram;
    context.node_sizes = node_sizes;
    context.title_sizes = subgraph_title_sizes;
    context.scale_factor = maxf(0.01f, scale_factor);
    context.node_gap = maxf(0, node_gap);
    context.rank_gap = maxf(0, rank_gap);
    context.result = &result;
    if (!scope_lists_build(&context) ||
        !layout_scope(&context, SIZE_MAX, diagram->direction,
                      &result.width, &result.height))
        tinta_mermaid_layout_destroy(&result);
    scope_lists_destroy(&context);
    return result;
}

void tinta_mermaid_layout_destroy(TintaMermaidLayout *layout) {
    if (!layout) return;
    free(layout->nodes);
    free(layout->subgraphs);
    free(layout->ranks);
    memset(layout, 0, sizeof(*layout));
}
