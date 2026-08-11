#include "math_opentype.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool range_valid(size_t length, size_t offset, size_t needed) {
    return offset <= length && needed <= length - offset;
}

static bool read_u16(const uint8_t *data, size_t length, size_t offset,
                     uint16_t *value) {
    if (!data || !value || !range_valid(length, offset, 2)) return false;
    *value = (uint16_t)(((uint16_t)data[offset] << 8) | data[offset + 1]);
    return true;
}

static bool read_i16(const uint8_t *data, size_t length, size_t offset,
                     int16_t *value) {
    uint16_t raw;
    if (!read_u16(data, length, offset, &raw)) return false;
    *value = (int16_t)raw;
    return true;
}

static bool read_u32(const uint8_t *data, size_t length, size_t offset,
                     uint32_t *value) {
    if (!data || !value || !range_valid(length, offset, 4)) return false;
    *value = ((uint32_t)data[offset] << 24) |
             ((uint32_t)data[offset + 1] << 16) |
             ((uint32_t)data[offset + 2] << 8) |
             (uint32_t)data[offset + 3];
    return true;
}

bool tinta_math_ot_parse(const uint8_t *table, size_t length,
                         TintaMathOtTable *result) {
    TintaMathOtTable parsed = {0};
    uint16_t overlap = 0;
    memset(&parsed, 0, sizeof(parsed));
    if (!table || !result || !range_valid(length, 0, 10) ||
        !read_u16(table, length, 0, &parsed.major_version) ||
        !read_u16(table, length, 2, &parsed.minor_version) ||
        !read_u16(table, length, 4, &parsed.constants_offset) ||
        !read_u16(table, length, 6, &parsed.glyph_info_offset) ||
        !read_u16(table, length, 8, &parsed.variants_offset) ||
        parsed.major_version != 1 ||
        !parsed.constants_offset || !parsed.glyph_info_offset ||
        !parsed.variants_offset ||
        !range_valid(length, parsed.constants_offset, 14) ||
        !range_valid(length, parsed.glyph_info_offset, 8) ||
        !range_valid(length, parsed.variants_offset, 10) ||
        !read_i16(table, length, parsed.constants_offset + 12,
                  &parsed.axis_height) ||
        !read_u16(table, length, parsed.variants_offset, &overlap))
        return false;
    parsed.minimum_connector_overlap = overlap;
    *result = parsed;
    return true;
}

bool tinta_math_ot_coverage_index(const uint8_t *table, size_t length,
                                  size_t coverage_offset, uint16_t glyph,
                                  uint16_t *coverage_index) {
    uint16_t format;
    uint16_t count;
    size_t i;
    if (!coverage_index ||
        !read_u16(table, length, coverage_offset, &format) ||
        !read_u16(table, length, coverage_offset + 2, &count))
        return false;
    if (format == 1) {
        if (!range_valid(length, coverage_offset + 4,
                         (size_t)count * 2)) return false;
        for (i = 0; i < count; i++) {
            uint16_t candidate;
            if (!read_u16(table, length, coverage_offset + 4 + i * 2,
                          &candidate)) return false;
            if (candidate == glyph) {
                *coverage_index = (uint16_t)i;
                return true;
            }
            if (candidate > glyph) return false;
        }
        return false;
    }
    if (format == 2) {
        if (!range_valid(length, coverage_offset + 4,
                         (size_t)count * 6)) return false;
        for (i = 0; i < count; i++) {
            uint16_t start;
            uint16_t end;
            uint16_t start_index;
            size_t record = coverage_offset + 4 + i * 6;
            uint32_t index;
            if (!read_u16(table, length, record, &start) ||
                !read_u16(table, length, record + 2, &end) ||
                !read_u16(table, length, record + 4, &start_index) ||
                start > end) return false;
            if (glyph < start) return false;
            if (glyph > end) continue;
            index = (uint32_t)start_index + glyph - start;
            if (index > UINT16_MAX) return false;
            *coverage_index = (uint16_t)index;
            return true;
        }
    }
    return false;
}

static bool construction_offset(const uint8_t *table, size_t length,
                                bool vertical, uint16_t glyph,
                                size_t *construction) {
    TintaMathOtTable math;
    uint16_t vertical_coverage;
    uint16_t horizontal_coverage;
    uint16_t vertical_count;
    uint16_t horizontal_count;
    uint16_t coverage_index;
    uint16_t relative;
    size_t coverage;
    size_t offsets;
    size_t count;
    if (!construction || !tinta_math_ot_parse(table, length, &math) ||
        !read_u16(table, length, math.variants_offset + 2,
                  &vertical_coverage) ||
        !read_u16(table, length, math.variants_offset + 4,
                  &horizontal_coverage) ||
        !read_u16(table, length, math.variants_offset + 6,
                  &vertical_count) ||
        !read_u16(table, length, math.variants_offset + 8,
                  &horizontal_count)) return false;
    coverage = math.variants_offset +
        (vertical ? vertical_coverage : horizontal_coverage);
    count = vertical ? vertical_count : horizontal_count;
    offsets = math.variants_offset + 10 +
        (vertical ? 0 : (size_t)vertical_count * 2);
    if (!range_valid(length, offsets, count * 2) ||
        !tinta_math_ot_coverage_index(table, length, coverage, glyph,
                                      &coverage_index) ||
        coverage_index >= count ||
        !read_u16(table, length, offsets + (size_t)coverage_index * 2,
                  &relative) || !relative ||
        !range_valid(length, math.variants_offset + relative, 4))
        return false;
    *construction = math.variants_offset + relative;
    return true;
}

bool tinta_math_ot_variant(const uint8_t *table, size_t length,
                           bool vertical, uint16_t glyph,
                           size_t variant_index,
                           TintaMathOtVariant *variant) {
    size_t construction;
    uint16_t count;
    size_t record;
    if (!variant ||
        !construction_offset(table, length, vertical, glyph,
                             &construction) ||
        !read_u16(table, length, construction + 2, &count) ||
        variant_index >= count) return false;
    record = construction + 4 + variant_index * 4;
    return read_u16(table, length, record, &variant->glyph) &&
           read_u16(table, length, record + 2, &variant->advance);
}

bool tinta_math_ot_assembly_part(const uint8_t *table, size_t length,
                                 bool vertical, uint16_t glyph,
                                 size_t part_index,
                                 TintaMathOtAssemblyPart *part) {
    size_t construction;
    size_t assembly;
    size_t record;
    uint16_t assembly_offset;
    uint16_t count;
    uint16_t flags;
    if (!part ||
        !construction_offset(table, length, vertical, glyph,
                             &construction) ||
        !read_u16(table, length, construction, &assembly_offset) ||
        !assembly_offset ||
        !range_valid(length, construction + assembly_offset, 6))
        return false;
    assembly = construction + assembly_offset;
    if (!read_u16(table, length, assembly + 4, &count) ||
        part_index >= count) return false;
    record = assembly + 6 + part_index * 10;
    if (!range_valid(length, record, 10) ||
        !read_u16(table, length, record, &part->glyph) ||
        !read_u16(table, length, record + 2, &part->start_connector) ||
        !read_u16(table, length, record + 4, &part->end_connector) ||
        !read_u16(table, length, record + 6, &part->full_advance) ||
        !read_u16(table, length, record + 8, &flags)) return false;
    part->extender = (flags & 1u) != 0;
    return true;
}

static bool sfnt_face_math(const uint8_t *data, size_t length,
                           size_t face_offset, TintaMathOtTable *result) {
    uint16_t table_count;
    size_t i;
    if (!range_valid(length, face_offset, 12) ||
        !read_u16(data, length, face_offset + 4, &table_count) ||
        !range_valid(length, face_offset + 12, (size_t)table_count * 16))
        return false;
    for (i = 0; i < table_count; i++) {
        size_t record = face_offset + 12 + i * 16;
        uint32_t offset;
        uint32_t table_length;
        if (memcmp(data + record, "MATH", 4) != 0) continue;
        if (!read_u32(data, length, record + 8, &offset) ||
            !read_u32(data, length, record + 12, &table_length) ||
            !range_valid(length, offset, table_length)) return false;
        return tinta_math_ot_parse(data + offset, table_length, result);
    }
    return false;
}

bool tinta_math_ot_font_file_valid(const wchar_t *path,
                                   TintaMathOtTable *result) {
    FILE *file;
    long file_size;
    uint8_t *data = NULL;
    bool valid = false;
    TintaMathOtTable parsed = {0};
    if (!path) return false;
    file = _wfopen(path, L"rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) ||
        (file_size = ftell(file)) <= 0 ||
        (uint64_t)file_size > 256u * 1024u * 1024u ||
        (uint64_t)file_size > SIZE_MAX ||
        fseek(file, 0, SEEK_SET)) goto done;
    data = (uint8_t *)malloc((size_t)file_size);
    if (!data || fread(data, 1, (size_t)file_size, file) !=
                 (size_t)file_size) goto done;
    if (range_valid((size_t)file_size, 0, 12) &&
        memcmp(data, "ttcf", 4) == 0) {
        uint32_t count;
        uint32_t i;
        if (!read_u32(data, (size_t)file_size, 8, &count) ||
            !range_valid((size_t)file_size, 12, 0) ||
            count > ((size_t)file_size - 12) / 4)
            goto done;
        for (i = 0; i < count; i++) {
            uint32_t face;
            if (!read_u32(data, (size_t)file_size, 12 + (size_t)i * 4,
                          &face)) goto done;
            if (sfnt_face_math(data, (size_t)file_size, face, &parsed)) {
                valid = true;
                break;
            }
        }
    } else {
        valid = sfnt_face_math(data, (size_t)file_size, 0, &parsed);
    }
done:
    free(data);
    fclose(file);
    if (valid && result) *result = parsed;
    return valid;
}
