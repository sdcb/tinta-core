#ifndef TINTA_MATH_H
#define TINTA_MATH_H

#include "tinta_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TintaMathNodeType {
    TINTA_MATH_ROW,
    TINTA_MATH_TEXT,
    TINTA_MATH_FRACTION,
    TINTA_MATH_ROOT,
    TINTA_MATH_SCRIPT,
    TINTA_MATH_DELIMITED,
    TINTA_MATH_ACCENT,
    TINTA_MATH_MATRIX,
    TINTA_MATH_STYLE,
    TINTA_MATH_SPACE
} TintaMathNodeType;

typedef enum TintaMathTextStyle {
    TINTA_MATH_STYLE_DEFAULT,
    TINTA_MATH_STYLE_ROMAN,
    TINTA_MATH_STYLE_BOLD,
    TINTA_MATH_STYLE_ITALIC,
    TINTA_MATH_STYLE_SANS,
    TINTA_MATH_STYLE_MONO,
    TINTA_MATH_STYLE_TEXT,
    TINTA_MATH_STYLE_DISPLAY,
    TINTA_MATH_STYLE_SCRIPT
} TintaMathTextStyle;

typedef struct TintaMathNode TintaMathNode;

struct TintaMathNode {
    TintaMathNodeType type;
    char *text;
    char *aux;
    float space_em;
    TintaMathTextStyle style;
    bool large_operator;
    TintaMathNode *a;
    TintaMathNode *b;
    TintaMathNode *c;
    TintaMathNode **children;
    size_t child_count;
    size_t child_capacity;
    size_t rows;
    size_t columns;
};

typedef struct TintaMathParseResult {
    TintaMathNode *root;
    bool success;
    size_t node_count;
    size_t error_offset;
} TintaMathParseResult;

TintaMathParseResult tinta_math_parse(const char *source, size_t length,
                                      size_t max_nodes, size_t max_depth);
void tinta_math_node_destroy(TintaMathNode *node);

#ifdef __cplusplus
}
#endif

#endif
