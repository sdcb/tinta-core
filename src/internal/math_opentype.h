#ifndef TINTA_MATH_OPENTYPE_H
#define TINTA_MATH_OPENTYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TintaMathOtTable {
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t constants_offset;
    uint16_t glyph_info_offset;
    uint16_t variants_offset;
    int16_t axis_height;
    uint16_t minimum_connector_overlap;
} TintaMathOtTable;

typedef struct TintaMathOtVariant {
    uint16_t glyph;
    uint16_t advance;
} TintaMathOtVariant;

typedef struct TintaMathOtAssemblyPart {
    uint16_t glyph;
    uint16_t start_connector;
    uint16_t end_connector;
    uint16_t full_advance;
    bool extender;
} TintaMathOtAssemblyPart;

bool tinta_math_ot_parse(const uint8_t *table, size_t length,
                         TintaMathOtTable *result);
bool tinta_math_ot_coverage_index(const uint8_t *table, size_t length,
                                  size_t coverage_offset, uint16_t glyph,
                                  uint16_t *coverage_index);
bool tinta_math_ot_variant(const uint8_t *table, size_t length,
                           bool vertical, uint16_t glyph,
                           size_t variant_index,
                           TintaMathOtVariant *variant);
bool tinta_math_ot_assembly_part(const uint8_t *table, size_t length,
                                 bool vertical, uint16_t glyph,
                                 size_t part_index,
                                 TintaMathOtAssemblyPart *part);
bool tinta_math_ot_font_file_valid(const wchar_t *path,
                                   TintaMathOtTable *result);

#ifdef __cplusplus
}
#endif

#endif
