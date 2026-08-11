#ifndef TINTA_MATH_LAYOUT_H
#define TINTA_MATH_LAYOUT_H

#include "tinta_math.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TintaMathGlyphMetrics {
    float width;
    float ascent;
    float descent;
} TintaMathGlyphMetrics;

typedef bool (*TintaMathMeasureFn)(void *context, const char *utf8,
                                   size_t length, float font_size,
                                   TintaMathTextStyle style,
                                   TintaMathGlyphMetrics *metrics);

typedef enum TintaMathPrimitiveType {
    TINTA_MATH_PRIMITIVE_TEXT,
    TINTA_MATH_PRIMITIVE_RULE
} TintaMathPrimitiveType;

typedef struct TintaMathPrimitive {
    TintaMathPrimitiveType type;
    char *text;
    size_t text_length;
    float x;
    float baseline;
    float font_size;
    TintaMathTextStyle style;
    float x2;
    float y2;
    float thickness;
} TintaMathPrimitive;

typedef struct TintaMathLayout {
    TintaMathPrimitive *primitives;
    size_t primitive_count;
    size_t primitive_capacity;
    float width;
    float ascent;
    float descent;
} TintaMathLayout;

bool tinta_math_layout_build(const TintaMathNode *root, float font_size,
                             bool display, TintaMathMeasureFn measure,
                             void *measure_context,
                             TintaMathLayout *layout);
void tinta_math_layout_destroy(TintaMathLayout *layout);

#ifdef __cplusplus
}
#endif

#endif
