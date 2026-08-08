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
    size_t parent_subgraph;
    bool has_definition;
} TintaMermaidNode;

typedef struct TintaMermaidSubgraph {
    char *id;
    char *label;
    char *class_name;
    TintaMermaidStyle style;
    TintaMermaidDirection direction;
    size_t parent_subgraph;
    size_t source_offset;
    bool has_direction;
} TintaMermaidSubgraph;

typedef struct TintaMermaidEdge {
    size_t from;
    size_t to;
    bool from_subgraph;
    bool to_subgraph;
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
    TintaMermaidSubgraph *subgraphs;
    size_t subgraph_count;
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

typedef struct TintaMermaidPoint {
    float x;
    float y;
} TintaMermaidPoint;

typedef struct TintaMermaidEdgeRoute {
    size_t point_offset;
    size_t point_count;
    float label_x;
    float label_y;
} TintaMermaidEdgeRoute;

typedef struct TintaMermaidLayout {
    TintaMermaidRect *nodes;
    size_t node_count;
    TintaMermaidRect *subgraphs;
    size_t subgraph_count;
    size_t *ranks;
    size_t rank_count;
    TintaMermaidEdgeRoute *edges;
    size_t edge_count;
    TintaMermaidPoint *points;
    size_t point_count;
    float width;
    float height;
} TintaMermaidLayout;

TintaMermaidParseResult tinta_mermaid_parse(const char *source, size_t length);
TintaMermaidParseResult tinta_mermaid_parse_limited(
    const char *source, size_t length, size_t max_nodes, size_t max_edges);
void tinta_mermaid_parse_result_destroy(TintaMermaidParseResult *result);
const TintaMermaidNode *tinta_mermaid_find_node(
    const TintaMermaidDiagram *diagram, const char *id);
const TintaMermaidSubgraph *tinta_mermaid_find_subgraph(
    const TintaMermaidDiagram *diagram, const char *id);
const TintaMermaidStyle *tinta_mermaid_find_class_style(
    const TintaMermaidDiagram *diagram, const char *name);

TintaMermaidLayout tinta_mermaid_layout(
    const TintaMermaidDiagram *diagram,
    const TintaMermaidSize *node_sizes,
    size_t node_size_count,
    const TintaMermaidSize *subgraph_title_sizes,
    size_t subgraph_title_size_count,
    float scale_factor,
    float node_gap,
    float rank_gap);
void tinta_mermaid_layout_destroy(TintaMermaidLayout *layout);

#ifdef __cplusplus
}
#endif

#endif
