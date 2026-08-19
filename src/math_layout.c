#include "math_layout.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct LayoutContext {
    TintaMathMeasureFn measure;
    void *measure_context;
    size_t depth;
} LayoutContext;

static float maxf(float a, float b) { return a > b ? a : b; }
static float minf(float a, float b) { return a < b ? a : b; }

typedef enum MathSpacingClass {
    MATH_SPACING_NONE,
    MATH_SPACING_ORDINARY,
    MATH_SPACING_OPERATOR,
    MATH_SPACING_BINARY,
    MATH_SPACING_RELATION,
    MATH_SPACING_OPEN,
    MATH_SPACING_CLOSE,
    MATH_SPACING_PUNCT,
    MATH_SPACING_SPACE
} MathSpacingClass;

static char *duplicate_bytes(const char *text, size_t length) {
    char *copy = (char *)malloc(length + 1);
    if (!copy) return NULL;
    if (length) memcpy(copy, text, length);
    copy[length] = 0;
    return copy;
}

static bool text_is_one_of(const char *text, const char *const *values,
                           size_t count) {
    size_t i;
    if (!text) return false;
    for (i = 0; i < count; i++)
        if (!strcmp(text, values[i])) return true;
    return false;
}

static bool text_is_multichar_ascii(const char *text) {
    size_t length;
    size_t i;
    if (!text) return false;
    length = strlen(text);
    if (length <= 1) return false;
    for (i = 0; i < length; i++)
        if ((unsigned char)text[i] >= 0x80) return false;
    return true;
}

static bool text_is_integral(const char *text) {
    return text && (!strcmp(text, "\xE2\x88\xAB") ||
                    !strcmp(text, "\xE2\x88\xAC") ||
                    !strcmp(text, "\xE2\x88\xAD") ||
                    !strcmp(text, "\xE2\x88\xAE"));
}

static MathSpacingClass spacing_class(const TintaMathNode *node) {
    static const char *const binary[] = {
        "+", "-", "\xC3\x97", "\xC3\xB7", "\xE2\x8B\x85",
        "\xC2\xB1", "\xE2\x88\x93", "\xE2\x88\x97", "\xE2\x8B\x86",
        "\xE2\x88\x98", "\xE2\x8A\x95", "\xE2\x8A\x97",
        "\xE2\x88\xA9", "\xE2\x88\xAA", "\xE2\x88\xA7",
        "\xE2\x88\xA8"
    };
    static const char *const relation[] = {
        "=", "<", ">", "\xE2\x89\xA4", "\xE2\x89\xA5",
        "\xE2\x89\xA0", "\xE2\x89\x88", "\xE2\x88\xBC",
        "\xE2\x89\xA1", "\xE2\x88\x9D", "\xE2\x88\x88",
        "\xE2\x88\x89", "\xE2\x88\x8B", "\xE2\x8A\x82",
        "\xE2\x8A\x83", "\xE2\x8A\x86", "\xE2\x8A\x87",
        "\xE2\x9F\x82", "\xE2\x88\xA5", "\xE2\x86\x92",
        "\xE2\x86\x90", "\xE2\x86\x94", "\xE2\x87\x92",
        "\xE2\x87\x90", "\xE2\x87\x94", "\xE2\x86\xA6"
    };
    static const char *const open[] = {
        "(", "[", "{", "\xE2\x9F\xA8"
    };
    static const char *const close[] = {
        ")", "]", "}", "\xE2\x9F\xA9"
    };
    if (!node) return MATH_SPACING_NONE;
    if (node->type == TINTA_MATH_SPACE) return MATH_SPACING_SPACE;
    if (node->type == TINTA_MATH_DELIMITED || node->type == TINTA_MATH_MATRIX)
        return MATH_SPACING_ORDINARY;
    if (node->type == TINTA_MATH_SCRIPT)
        return spacing_class(node->a);
    if (node->type == TINTA_MATH_STYLE)
        return spacing_class(node->a);
    if (node->type != TINTA_MATH_TEXT)
        return MATH_SPACING_ORDINARY;
    if (node->large_operator || text_is_multichar_ascii(node->text))
        return MATH_SPACING_OPERATOR;
    if (text_is_one_of(node->text, binary,
                       sizeof(binary) / sizeof(binary[0])))
        return MATH_SPACING_BINARY;
    if (text_is_one_of(node->text, relation,
                       sizeof(relation) / sizeof(relation[0])))
        return MATH_SPACING_RELATION;
    if (text_is_one_of(node->text, open, sizeof(open) / sizeof(open[0])))
        return MATH_SPACING_OPEN;
    if (text_is_one_of(node->text, close, sizeof(close) / sizeof(close[0])))
        return MATH_SPACING_CLOSE;
    if (node->text && (!strcmp(node->text, ",") || !strcmp(node->text, ";") ||
                       !strcmp(node->text, ":") || !strcmp(node->text, "|") ||
                       !strcmp(node->text, "\xE2\x80\x96")))
        return MATH_SPACING_PUNCT;
    return MATH_SPACING_ORDINARY;
}

static float implicit_spacing_em(MathSpacingClass left,
                                 MathSpacingClass right) {
    if (left == MATH_SPACING_NONE || right == MATH_SPACING_NONE ||
        left == MATH_SPACING_SPACE || right == MATH_SPACING_SPACE ||
        left == MATH_SPACING_OPEN || right == MATH_SPACING_CLOSE ||
        right == MATH_SPACING_PUNCT)
        return 0;
    if (left == MATH_SPACING_RELATION || right == MATH_SPACING_RELATION)
        return 0.26f;
    if (left == MATH_SPACING_BINARY || right == MATH_SPACING_BINARY)
        return 0.20f;
    if (left == MATH_SPACING_PUNCT)
        return 0.12f;
    if (left == MATH_SPACING_OPERATOR || right == MATH_SPACING_OPERATOR)
        return 0.16f;
    return 0;
}

static float style_size_scale(TintaMathTextStyle style) {
    if (style == TINTA_MATH_STYLE_DISPLAY) return 1.06f;
    if (style == TINTA_MATH_STYLE_SCRIPT) return 0.70f;
    if (style == TINTA_MATH_STYLE_SCRIPTSCRIPT) return 0.52f;
    return 1.0f;
}

static size_t utf8_encode(unsigned codepoint, char out[5]) {
    if (codepoint <= 0x7F) {
        out[0] = (char)codepoint;
        out[1] = 0;
        return 1;
    }
    if (codepoint <= 0x7FF) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        out[2] = 0;
        return 2;
    }
    if (codepoint <= 0xFFFF) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        out[3] = 0;
        return 3;
    }
    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    out[4] = 0;
    return 4;
}

static size_t map_math_alpha(TintaMathTextStyle style, const char *text,
                             char mapped[5]) {
    unsigned char ch;
    unsigned codepoint = 0;
    if (!text || text[0] == 0 || text[1] != 0) return 0;
    ch = (unsigned char)text[0];
    if (style == TINTA_MATH_STYLE_BLACKBOARD) {
        if (ch >= 'A' && ch <= 'Z') {
            switch (ch) {
                case 'C': codepoint = 0x2102; break;
                case 'H': codepoint = 0x210D; break;
                case 'N': codepoint = 0x2115; break;
                case 'P': codepoint = 0x2119; break;
                case 'Q': codepoint = 0x211A; break;
                case 'R': codepoint = 0x211D; break;
                case 'Z': codepoint = 0x2124; break;
                default: codepoint = 0x1D538 + (unsigned)(ch - 'A'); break;
            }
        } else if (ch >= 'a' && ch <= 'z')
            codepoint = 0x1D552 + (unsigned)(ch - 'a');
        else if (ch >= '0' && ch <= '9')
            codepoint = 0x1D7D8 + (unsigned)(ch - '0');
    } else if (style == TINTA_MATH_STYLE_CALLIGRAPHIC) {
        if (ch >= 'A' && ch <= 'Z') {
            switch (ch) {
                case 'B': codepoint = 0x212C; break;
                case 'E': codepoint = 0x2130; break;
                case 'F': codepoint = 0x2131; break;
                case 'H': codepoint = 0x210B; break;
                case 'I': codepoint = 0x2110; break;
                case 'L': codepoint = 0x2112; break;
                case 'M': codepoint = 0x2133; break;
                case 'R': codepoint = 0x211B; break;
                default: codepoint = 0x1D49C + (unsigned)(ch - 'A'); break;
            }
        } else if (ch >= 'a' && ch <= 'z')
            codepoint = 0x1D4B6 + (unsigned)(ch - 'a');
    }
    return codepoint ? utf8_encode(codepoint, mapped) : 0;
}

void tinta_math_layout_destroy(TintaMathLayout *layout) {
    size_t i;
    if (!layout) return;
    for (i = 0; i < layout->primitive_count; i++)
        free(layout->primitives[i].text);
    free(layout->primitives);
    memset(layout, 0, sizeof(*layout));
}

static bool reserve_primitives(TintaMathLayout *layout, size_t add) {
    size_t needed = layout->primitive_count + add;
    size_t capacity;
    TintaMathPrimitive *items;
    if (needed <= layout->primitive_capacity) return true;
    capacity = layout->primitive_capacity ? layout->primitive_capacity : 8;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return false;
        capacity *= 2;
    }
    items = (TintaMathPrimitive *)realloc(
        layout->primitives, capacity * sizeof(*items));
    if (!items) return false;
    layout->primitives = items;
    layout->primitive_capacity = capacity;
    return true;
}

static bool add_text(TintaMathLayout *layout, const char *text, size_t length,
                     float x, float baseline, float font_size,
                     TintaMathTextStyle style) {
    TintaMathPrimitive item;
    memset(&item, 0, sizeof(item));
    item.type = TINTA_MATH_PRIMITIVE_TEXT;
    item.text = duplicate_bytes(text, length);
    if (!item.text || !reserve_primitives(layout, 1)) {
        free(item.text);
        return false;
    }
    item.text_length = length;
    item.x = x;
    item.baseline = baseline;
    item.font_size = font_size;
    item.style = style;
    layout->primitives[layout->primitive_count++] = item;
    return true;
}

static bool add_rule(TintaMathLayout *layout, float x1, float y1,
                     float x2, float y2, float thickness) {
    TintaMathPrimitive item;
    memset(&item, 0, sizeof(item));
    if (!reserve_primitives(layout, 1)) return false;
    item.type = TINTA_MATH_PRIMITIVE_RULE;
    item.x = x1;
    item.baseline = y1;
    item.x2 = x2;
    item.y2 = y2;
    item.thickness = thickness;
    layout->primitives[layout->primitive_count++] = item;
    return true;
}

static bool merge_layout(TintaMathLayout *target, TintaMathLayout *source,
                         float dx, float dy) {
    size_t i;
    if (!reserve_primitives(target, source->primitive_count)) return false;
    for (i = 0; i < source->primitive_count; i++) {
        TintaMathPrimitive item = source->primitives[i];
        item.x += dx;
        item.x2 += dx;
        item.baseline += dy;
        item.y2 += dy;
        target->primitives[target->primitive_count++] = item;
        source->primitives[i].text = NULL;
    }
    free(source->primitives);
    source->primitives = NULL;
    source->primitive_count = 0;
    source->primitive_capacity = 0;
    return true;
}

static bool layout_node(LayoutContext *context, const TintaMathNode *node,
                        float font_size, TintaMathTextStyle inherited_style,
                        bool display, TintaMathLayout *layout);

static bool measure_text(LayoutContext *context, const char *text,
                         size_t length, float font_size,
                         TintaMathTextStyle style,
                         TintaMathGlyphMetrics *metrics) {
    memset(metrics, 0, sizeof(*metrics));
    if (!length) return true;
    return context->measure(context->measure_context, text, length,
                            font_size, style, metrics);
}

static bool layout_text(LayoutContext *context, const TintaMathNode *node,
                        float font_size, TintaMathTextStyle inherited_style,
                        bool display, TintaMathLayout *layout) {
    TintaMathGlyphMetrics metrics;
    TintaMathTextStyle style = node->style == TINTA_MATH_STYLE_DEFAULT ?
        inherited_style : node->style;
    float actual_size = font_size;
    const char *text = node->text ? node->text : "";
    char mapped[5];
    size_t length = map_math_alpha(style, text, mapped);
    if (length) text = mapped;
    else length = strlen(text);
    if (style == TINTA_MATH_STYLE_DEFAULT && length == 1 &&
        ((text[0] >= 'a' && text[0] <= 'z') ||
         (text[0] >= 'A' && text[0] <= 'Z')))
        style = TINTA_MATH_STYLE_ITALIC;
    if (node->large_operator && text[0] &&
        (unsigned char)text[0] >= 0x80) {
        style = TINTA_MATH_STYLE_SYMBOL;
        actual_size *= display ? 1.12f : 1.03f;
    }
    if (!measure_text(context, text, length,
                      actual_size, style, &metrics) ||
        !add_text(layout, text, length, 0, 0,
                  actual_size, style)) return false;
    layout->width = metrics.width;
    layout->ascent = metrics.ascent;
    layout->descent = metrics.descent;
    return true;
}

static bool layout_row(LayoutContext *context, const TintaMathNode *node,
                       float font_size, TintaMathTextStyle style,
                       bool display, TintaMathLayout *layout) {
    size_t i;
    float current_size = font_size;
    bool current_display = display;
    MathSpacingClass previous_class = MATH_SPACING_NONE;
    for (i = 0; i < node->child_count; i++) {
        TintaMathLayout child = {0};
        const TintaMathNode *child_node = node->children[i];
        MathSpacingClass child_class;
        float spacing;
        if (child_node->type == TINTA_MATH_SPACE &&
            child_node->space_em == 0) {
            if (child_node->style == TINTA_MATH_STYLE_DISPLAY)
                current_display = true;
            else if (child_node->style == TINTA_MATH_STYLE_SCRIPT)
                current_size = font_size * 0.70f;
            else if (child_node->style == TINTA_MATH_STYLE_SCRIPTSCRIPT)
                current_size = font_size * 0.52f;
        }
        if (!layout_node(context, child_node, current_size, style,
                         current_display, &child)) {
            tinta_math_layout_destroy(&child);
            return false;
        }
        child_class = spacing_class(child_node);
        spacing = implicit_spacing_em(previous_class, child_class) *
                  current_size;
        layout->width += spacing;
        layout->ascent = maxf(layout->ascent, child.ascent);
        layout->descent = maxf(layout->descent, child.descent);
        if (!merge_layout(layout, &child, layout->width, 0)) {
            tinta_math_layout_destroy(&child);
            return false;
        }
        layout->width += child.width;
        if (child_class != MATH_SPACING_SPACE &&
            !(child_node->type == TINTA_MATH_SPACE &&
              child_node->space_em == 0))
            previous_class = child_class;
    }
    return true;
}

static bool layout_fraction(LayoutContext *context, const TintaMathNode *node,
                            float font_size, TintaMathTextStyle style,
                            bool display, TintaMathLayout *layout) {
    TintaMathLayout numerator = {0};
    TintaMathLayout denominator = {0};
    float child_scale = node->style == TINTA_MATH_STYLE_SCRIPT ? 0.72f :
                        node->style == TINTA_MATH_STYLE_SCRIPTSCRIPT ? 0.58f :
                        display || node->style == TINTA_MATH_STYLE_DISPLAY ?
                        0.92f : 0.72f;
    float child_size = maxf(8.0f, font_size * child_scale);
    float padding = font_size * 0.12f;
    float gap = font_size * (display ? 0.14f : 0.10f);
    float rule = node->space_em < 0 ? 0 : maxf(1.0f, font_size * 0.055f);
    float rule_y = -font_size * 0.26f;
    float numerator_baseline;
    float denominator_baseline;
    if (!node->a || !node->b ||
        !layout_node(context, node->a, child_size, style, false, &numerator) ||
        !layout_node(context, node->b, child_size, style, false, &denominator))
        goto failed;
    layout->width = maxf(numerator.width, denominator.width) + padding * 2;
    numerator_baseline = rule_y - rule * 0.5f - gap - numerator.descent;
    denominator_baseline = rule_y + rule * 0.5f + gap + denominator.ascent;
    layout->ascent = -numerator_baseline + numerator.ascent;
    layout->descent = denominator_baseline + denominator.descent;
    if (!merge_layout(layout, &numerator,
                      (layout->width - numerator.width) * 0.5f,
                      numerator_baseline) ||
        !merge_layout(layout, &denominator,
                      (layout->width - denominator.width) * 0.5f,
                      denominator_baseline) ||
        (rule > 0 && !add_rule(layout, 0, rule_y,
                              layout->width, rule_y, rule)))
        goto failed;
    return true;
failed:
    tinta_math_layout_destroy(&numerator);
    tinta_math_layout_destroy(&denominator);
    return false;
}

static bool layout_script(LayoutContext *context, const TintaMathNode *node,
                          float font_size, TintaMathTextStyle style,
                          bool display, TintaMathLayout *layout) {
    TintaMathLayout base = {0}, sub = {0}, sup = {0};
    float script_size = maxf(8.0f, font_size * 0.68f);
    float script_x;
    float sub_baseline = 0;
    float sup_baseline = 0;
    bool limits = node->limits_mode == TINTA_MATH_LIMITS_LIMITS ||
        (node->limits_mode == TINTA_MATH_LIMITS_AUTO && display &&
         node->a && node->a->type == TINTA_MATH_TEXT &&
         node->a->large_operator && !text_is_integral(node->a->text));
    if (!node->a || !layout_node(context, node->a, font_size, style,
                                 display, &base)) goto failed;
    if (node->b && !layout_node(context, node->b, script_size,
                                TINTA_MATH_STYLE_SCRIPT, false, &sub))
        goto failed;
    if (node->c && !layout_node(context, node->c, script_size,
                                TINTA_MATH_STYLE_SCRIPT, false, &sup))
        goto failed;
    script_x = limits ? 0 : base.width + font_size * 0.03f;
    if (limits) {
        float gap = font_size * 0.05f;
        layout->width = maxf(base.width, maxf(sub.width, sup.width));
        if (node->c)
            sup_baseline = -base.ascent - gap - sup.descent;
        if (node->b)
            sub_baseline = base.descent + gap + sub.ascent;
        layout->ascent = maxf(base.ascent,
            node->c ? -sup_baseline + sup.ascent : 0);
        layout->descent = maxf(base.descent,
            node->b ? sub_baseline + sub.descent : 0);
        if (!merge_layout(layout, &base,
                          (layout->width - base.width) * 0.5f, 0) ||
            (node->b && !merge_layout(layout, &sub,
                (layout->width - sub.width) * 0.5f, sub_baseline)) ||
            (node->c && !merge_layout(layout, &sup,
                (layout->width - sup.width) * 0.5f, sup_baseline)))
            goto failed;
        return true;
    }
    if (node->c)
        sup_baseline = -maxf(font_size * 0.38f, base.ascent * 0.55f);
    if (node->b)
        sub_baseline = maxf(font_size * 0.16f,
                            base.descent + font_size * 0.05f);
    if (node->b && node->c) {
        float overlap = sup_baseline + sup.descent + font_size * 0.12f -
                        (sub_baseline - sub.ascent);
        if (overlap > 0) {
            sup_baseline -= overlap * 0.5f;
            sub_baseline += overlap * 0.5f;
        }
    }
    layout->width = base.width + (node->b || node->c ?
        font_size * 0.03f + maxf(sub.width, sup.width) : 0);
    layout->ascent = maxf(base.ascent,
        node->c ? -sup_baseline + sup.ascent : 0);
    layout->descent = maxf(base.descent,
        node->b ? sub_baseline + sub.descent : 0);
    if (!merge_layout(layout, &base, 0, 0) ||
        (node->b && !merge_layout(layout, &sub, script_x, sub_baseline)) ||
        (node->c && !merge_layout(layout, &sup, script_x, sup_baseline)))
        goto failed;
    return true;
failed:
    tinta_math_layout_destroy(&base);
    tinta_math_layout_destroy(&sub);
    tinta_math_layout_destroy(&sup);
    return false;
}

static bool delimiter_layout(LayoutContext *context, const char *delimiter,
                             float font_size, float target_height,
                             TintaMathTextStyle style,
                             TintaMathLayout *layout) {
    TintaMathNode synthetic;
    if (!delimiter || !strcmp(delimiter, ".")) return true;
    memset(&synthetic, 0, sizeof(synthetic));
    synthetic.type = TINTA_MATH_TEXT;
    synthetic.text = (char *)delimiter;
    synthetic.style = TINTA_MATH_STYLE_SYMBOL;
    if (target_height > font_size * 1.15f)
        font_size *= minf(2.4f,
                          target_height / maxf(font_size, 1.0f));
    return layout_text(context, &synthetic, font_size, style, false, layout);
}

static bool wrap_delimiters(LayoutContext *context, TintaMathLayout *body,
                            const char *left, const char *right,
                            float font_size, TintaMathTextStyle style,
                            TintaMathLayout *layout) {
    TintaMathLayout left_box = {0}, right_box = {0};
    float target = body->ascent + body->descent;
    float body_center;
    float left_dy;
    float right_dy;
    if (!delimiter_layout(context, left, font_size, target, style, &left_box) ||
        !delimiter_layout(context, right, font_size, target, style, &right_box))
        goto failed;
    body_center = (body->descent - body->ascent) * 0.5f;
    left_dy = body_center -
        (left_box.descent - left_box.ascent) * 0.5f;
    right_dy = body_center -
        (right_box.descent - right_box.ascent) * 0.5f;
    layout->width = left_box.width + body->width + right_box.width;
    layout->ascent = maxf(body->ascent,
        maxf(left_box.ascent - left_dy, right_box.ascent - right_dy));
    layout->descent = maxf(body->descent,
        maxf(left_box.descent + left_dy, right_box.descent + right_dy));
    if (!merge_layout(layout, &left_box, 0, left_dy) ||
        !merge_layout(layout, body, left_box.width, 0) ||
        !merge_layout(layout, &right_box,
                      left_box.width + body->width, right_dy)) goto failed;
    return true;
failed:
    tinta_math_layout_destroy(&left_box);
    tinta_math_layout_destroy(&right_box);
    return false;
}

static bool layout_root(LayoutContext *context, const TintaMathNode *node,
                        float font_size, TintaMathTextStyle style,
                        bool display, TintaMathLayout *layout) {
    TintaMathLayout body = {0}, radical = {0}, degree = {0};
    TintaMathNode radical_node;
    float radical_size;
    float degree_x = 0;
    float body_x;
    float bar_y;
    float radical_dy;
    memset(&radical_node, 0, sizeof(radical_node));
    radical_node.type = TINTA_MATH_TEXT;
    radical_node.text = "\xE2\x88\x9A";
    radical_node.style = TINTA_MATH_STYLE_SYMBOL;
    if (!node->a || !layout_node(context, node->a, font_size, style,
                                 display, &body)) goto failed;
    radical_size = font_size * minf(3.0f, maxf(1.0f,
        (body.ascent + body.descent) / maxf(font_size, 1.0f)));
    if (!layout_text(context, &radical_node, radical_size, style,
                     false, &radical)) goto failed;
    if (node->b && !layout_node(context, node->b, font_size * 0.55f,
                                TINTA_MATH_STYLE_SCRIPT, false, &degree))
        goto failed;
    degree_x = degree.width ? degree.width * 0.65f : 0;
    body_x = degree_x + radical.width * 0.95f;
    bar_y = -body.ascent - font_size * 0.10f;
    radical_dy = body.descent - radical.descent;
    layout->width = body_x + body.width + font_size * 0.15f;
    layout->ascent = maxf(radical.ascent - radical_dy,
        maxf(-bar_y + font_size * 0.05f,
             degree.width ? radical.ascent - radical_dy +
                                degree.ascent * 0.35f : 0));
    layout->descent = maxf(body.descent, radical.descent + radical_dy);
    if (!merge_layout(layout, &radical, degree_x, radical_dy) ||
        !merge_layout(layout, &body, body_x, 0) ||
        (degree.width && !merge_layout(layout, &degree, 0,
             radical_dy - radical.ascent * 0.62f)) ||
        !add_rule(layout, body_x - font_size * 0.05f, bar_y,
                  layout->width, bar_y,
                  maxf(1.0f, font_size * 0.05f))) goto failed;
    return true;
failed:
    tinta_math_layout_destroy(&body);
    tinta_math_layout_destroy(&radical);
    tinta_math_layout_destroy(&degree);
    return false;
}

static bool layout_accent(LayoutContext *context, const TintaMathNode *node,
                          float font_size, TintaMathTextStyle style,
                          bool display, TintaMathLayout *layout) {
    TintaMathLayout body = {0}, accent = {0};
    const char *mark = node->text ? node->text : "^";
    bool over_rule = !strcmp(mark, "_over");
    bool under_rule = !strcmp(mark, "_under");
    bool under = under_rule || !strcmp(mark, "_underbrace");
    bool vector = !strcmp(mark, "\xE2\x86\x92");
    bool hat = !strcmp(mark, "^");
    const char *glyph = !strcmp(mark, "_overbrace") ? "\xE2\x8F\x9E" :
                        !strcmp(mark, "_underbrace") ? "\xE2\x8F\x9F" : mark;
    if (!node->a || !layout_node(context, node->a, font_size, style,
                                 display, &body)) goto failed;
    if (!over_rule && !under_rule && !vector && !hat) {
        TintaMathNode accent_node;
        memset(&accent_node, 0, sizeof(accent_node));
        accent_node.type = TINTA_MATH_TEXT;
        accent_node.text = (char *)glyph;
        if (!layout_text(context, &accent_node, font_size * 0.82f,
                         style, false, &accent)) goto failed;
    }
    layout->width = maxf(body.width, accent.width);
    layout->ascent = body.ascent;
    layout->descent = body.descent;
    if (vector || hat) {
        float gap = font_size * 0.04f;
        float rule = maxf(1.0f, font_size * 0.04f);
        float top = -body.ascent - gap - rule;
        float center = top + rule * 0.5f;
        float arrow = font_size * 0.16f;
        if (!merge_layout(layout, &body, 0, 0)) goto failed;
        layout->width = body.width;
        layout->ascent = body.ascent + gap + rule + font_size * 0.03f;
        layout->descent = body.descent;
        if (vector) {
            if (!add_rule(layout, 0, center, layout->width, center, rule) ||
                !add_rule(layout, layout->width - arrow,
                          center - arrow * 0.6f,
                          layout->width, center, rule) ||
                !add_rule(layout, layout->width - arrow,
                          center + arrow * 0.6f,
                          layout->width, center, rule)) goto failed;
        } else {
            float middle = layout->width * 0.5f;
            float half = minf(layout->width * 0.5f, font_size * 0.22f);
            float bottom = top + rule + font_size * 0.05f;
            if (!add_rule(layout, middle - half, bottom,
                          middle, top, rule) ||
                !add_rule(layout, middle, top,
                          middle + half, bottom, rule)) goto failed;
        }
    } else if (over_rule || under_rule) {
        float y = under ? body.descent + font_size * 0.08f :
                          -body.ascent - font_size * 0.08f;
        if (under) layout->descent = y + font_size * 0.08f;
        else layout->ascent = -y + font_size * 0.08f;
        if (!merge_layout(layout, &body,
                          (layout->width - body.width) * 0.5f, 0) ||
            !add_rule(layout, 0, y, layout->width, y,
                      maxf(1.0f, font_size * 0.05f))) goto failed;
    } else {
        float baseline = under ? body.descent + accent.ascent :
                                 -body.ascent - accent.descent;
        if (under) layout->descent = baseline + accent.descent;
        else layout->ascent = -baseline + accent.ascent;
        if (!merge_layout(layout, &body,
                          (layout->width - body.width) * 0.5f, 0) ||
            !merge_layout(layout, &accent,
                          (layout->width - accent.width) * 0.5f,
                          baseline)) goto failed;
    }
    return true;
failed:
    tinta_math_layout_destroy(&body);
    tinta_math_layout_destroy(&accent);
    return false;
}

static bool layout_matrix(LayoutContext *context, const TintaMathNode *node,
                          float font_size, TintaMathTextStyle style,
                          bool display, TintaMathLayout *layout) {
    TintaMathLayout *cells = NULL;
    float *column_widths = NULL;
    float *row_ascents = NULL;
    float *row_descents = NULL;
    TintaMathLayout body = {0};
    size_t count;
    size_t i;
    float column_gap = font_size * 0.78f;
    float row_gap = font_size * 0.34f;
    float total_height = 0;
    float baseline;
    if (!node->rows || !node->columns ||
        node->rows > SIZE_MAX / node->columns) return false;
    count = node->rows * node->columns;
    cells = (TintaMathLayout *)calloc(count, sizeof(*cells));
    column_widths = (float *)calloc(node->columns, sizeof(*column_widths));
    row_ascents = (float *)calloc(node->rows, sizeof(*row_ascents));
    row_descents = (float *)calloc(node->rows, sizeof(*row_descents));
    if (!cells || !column_widths || !row_ascents || !row_descents) goto failed;
    for (i = 0; i < count; i++) {
        size_t row = i / node->columns;
        size_t column = i % node->columns;
        if (i >= node->child_count ||
            !layout_node(context, node->children[i], font_size * 0.94f,
                         style, display, &cells[i])) goto failed;
        column_widths[column] = maxf(column_widths[column], cells[i].width);
        row_ascents[row] = maxf(row_ascents[row], cells[i].ascent);
        row_descents[row] = maxf(row_descents[row], cells[i].descent);
    }
    for (i = 0; i < node->columns; i++)
        body.width += column_widths[i] + (i ? column_gap : 0);
    for (i = 0; i < node->rows; i++)
        total_height += row_ascents[i] + row_descents[i] +
                        (i ? row_gap : 0);
    body.ascent = total_height * 0.5f + font_size * 0.12f;
    body.descent = total_height - body.ascent;
    baseline = -body.ascent;
    for (i = 0; i < node->rows; i++) {
        size_t column;
        float x = 0;
        baseline += row_ascents[i];
        for (column = 0; column < node->columns; column++) {
            size_t index = i * node->columns + column;
            if (!merge_layout(&body, &cells[index],
                    x + (column_widths[column] - cells[index].width) * 0.5f,
                    baseline)) goto failed;
            x += column_widths[column] + column_gap;
        }
        baseline += row_descents[i] + row_gap;
    }
    if (!wrap_delimiters(context, &body, node->text, node->aux,
                         font_size, style, layout)) goto failed;
    free(cells); free(column_widths); free(row_ascents); free(row_descents);
    return true;
failed:
    if (cells) for (i = 0; i < count; i++) tinta_math_layout_destroy(&cells[i]);
    tinta_math_layout_destroy(&body);
    free(cells); free(column_widths); free(row_ascents); free(row_descents);
    return false;
}

static bool layout_node(LayoutContext *context, const TintaMathNode *node,
                        float font_size, TintaMathTextStyle inherited_style,
                        bool display, TintaMathLayout *layout) {
    bool result = false;
    if (!node || !layout || context->depth >= 512) return false;
    context->depth++;
    switch (node->type) {
        case TINTA_MATH_ROW:
            result = layout_row(context, node, font_size, inherited_style,
                                display, layout);
            break;
        case TINTA_MATH_TEXT:
            result = layout_text(context, node, font_size, inherited_style,
                                 display, layout);
            break;
        case TINTA_MATH_SPACE:
            layout->width = font_size * node->space_em;
            result = true;
            break;
        case TINTA_MATH_FRACTION:
            result = layout_fraction(context, node, font_size,
                                     inherited_style, display, layout);
            break;
        case TINTA_MATH_ROOT:
            result = layout_root(context, node, font_size, inherited_style,
                                 display, layout);
            break;
        case TINTA_MATH_SCRIPT:
            result = layout_script(context, node, font_size, inherited_style,
                                   display, layout);
            break;
        case TINTA_MATH_DELIMITED: {
            TintaMathLayout body = {0};
            if (node->a && layout_node(context, node->a, font_size,
                    inherited_style, display, &body))
                result = wrap_delimiters(context, &body, node->text,
                    node->aux, font_size, inherited_style, layout);
            tinta_math_layout_destroy(&body);
            break;
        }
        case TINTA_MATH_ACCENT:
            result = layout_accent(context, node, font_size, inherited_style,
                                   display, layout);
            break;
        case TINTA_MATH_MATRIX:
            result = layout_matrix(context, node, font_size, inherited_style,
                                   display, layout);
            break;
        case TINTA_MATH_STYLE:
            result = node->a && layout_node(
                context, node->a,
                font_size * style_size_scale(node->style),
                node->style == TINTA_MATH_STYLE_DISPLAY ||
                    node->style == TINTA_MATH_STYLE_SCRIPT ||
                    node->style == TINTA_MATH_STYLE_SCRIPTSCRIPT ?
                    inherited_style : node->style,
                display || node->style == TINTA_MATH_STYLE_DISPLAY,
                layout);
            break;
        default:
            break;
    }
    context->depth--;
    if (!result) tinta_math_layout_destroy(layout);
    return result;
}

bool tinta_math_layout_build(const TintaMathNode *root, float font_size,
                             bool display, TintaMathMeasureFn measure,
                             void *measure_context,
                             TintaMathLayout *layout) {
    LayoutContext context;
    bool result;
    size_t i;
    if (!root || !measure || !layout || !isfinite(font_size) || font_size <= 0)
        return false;
    memset(layout, 0, sizeof(*layout));
    memset(&context, 0, sizeof(context));
    context.measure = measure;
    context.measure_context = measure_context;
    result = layout_node(&context, root, font_size,
                         TINTA_MATH_STYLE_DEFAULT, display, layout);
    if (result && display) {
        float padding = font_size * 0.15f;
        for (i = 0; i < layout->primitive_count; i++) {
            layout->primitives[i].x += padding;
            layout->primitives[i].x2 += padding;
        }
        layout->width += padding * 2;
        layout->ascent += padding;
        layout->descent += padding;
    }
    return result;
}
