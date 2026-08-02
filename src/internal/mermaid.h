#ifndef TINTA_MERMAID_H
#define TINTA_MERMAID_H

#include "tinta_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TintaMermaidDirection {
    TINTA_MERMAID_TOP_TO_BOTTOM,
    TINTA_MERMAID_BOTTOM_TO_TOP,
    TINTA_MERMAID_LEFT_TO_RIGHT,
    TINTA_MERMAID_RIGHT_TO_LEFT
} TintaMermaidDirection;

typedef enum TintaMermaidNodeShape {
    TINTA_MERMAID_RECTANGLE,
    TINTA_MERMAID_ROUNDED_RECTANGLE,
    TINTA_MERMAID_DIAMOND,
    TINTA_MERMAID_STADIUM,
    TINTA_MERMAID_CIRCLE,
    TINTA_MERMAID_HEXAGON
} TintaMermaidNodeShape;

typedef struct TintaMermaidColor {
    uint32_t rgb;
    float alpha;
} TintaMermaidColor;

typedef struct TintaMermaidStyle {
    bool has_fill;
    bool has_stroke;
    bool has_text;
    bool has_stroke_width;
    TintaMermaidColor fill;
    TintaMermaidColor stroke;
    TintaMermaidColor text;
    float stroke_width;
} TintaMermaidStyle;

typedef struct TintaMermaidNode {
    char *id;
    char *label;
    TintaMermaidNodeShape shape;
    char *class_name;
    TintaMermaidStyle style;
    size_t source_offset;
} TintaMermaidNode;

typedef struct TintaMermaidEdge {
    size_t from;
    size_t to;
    char *label;
    bool directed;
    bool dashed;
    float stroke_scale;
} TintaMermaidEdge;

typedef struct TintaMermaidClassStyle {
    char *name;
    TintaMermaidStyle style;
} TintaMermaidClassStyle;

typedef struct TintaMermaidDiagram {
    TintaMermaidDirection direction;
    TintaMermaidNode *nodes;
    size_t node_count;
    TintaMermaidEdge *edges;
    size_t edge_count;
    TintaMermaidClassStyle *class_styles;
    size_t class_style_count;
} TintaMermaidDiagram;

typedef struct TintaMermaidParseResult {
    TintaMermaidDiagram diagram;
    bool success;
    size_t error_line;
    char *error;
} TintaMermaidParseResult;

typedef struct TintaMermaidSize {
    float width;
    float height;
} TintaMermaidSize;

typedef struct TintaMermaidRect {
    float left;
    float top;
    float right;
    float bottom;
} TintaMermaidRect;

typedef struct TintaMermaidLayout {
    TintaMermaidRect *nodes;
    size_t node_count;
    size_t *ranks;
    size_t rank_count;
    float width;
    float height;
} TintaMermaidLayout;

TintaMermaidParseResult tinta_mermaid_parse(const char *source, size_t length);
void tinta_mermaid_parse_result_destroy(TintaMermaidParseResult *result);
const TintaMermaidNode *tinta_mermaid_find_node(
    const TintaMermaidDiagram *diagram, const char *id);
const TintaMermaidStyle *tinta_mermaid_find_class_style(
    const TintaMermaidDiagram *diagram, const char *name);

TintaMermaidLayout tinta_mermaid_layout(
    const TintaMermaidDiagram *diagram,
    const TintaMermaidSize *node_sizes,
    size_t node_size_count,
    float node_gap,
    float rank_gap);
void tinta_mermaid_layout_destroy(TintaMermaidLayout *layout);

#ifdef __cplusplus
}
#endif

#endif

