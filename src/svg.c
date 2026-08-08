#include "svg.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool ascii_equal_n(const char *left, const char *right, size_t length) {
    size_t i;
    for (i = 0; i < length; i++) {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return true;
}

static const char *ascii_find_nocase(const char *data, size_t length,
                                     const char *needle) {
    size_t needle_length = strlen(needle);
    size_t i;
    if (!needle_length || needle_length > length) return NULL;
    for (i = 0; i + needle_length <= length; i++)
        if (ascii_equal_n(data + i, needle, needle_length)) return data + i;
    return NULL;
}

bool tinta_svg_data_uri(const char *uri) {
    static const char prefix[] = "data:image/svg+xml";
    size_t length = sizeof(prefix) - 1;
    return uri && strlen(uri) > length &&
           ascii_equal_n(uri, prefix, length) &&
           (uri[length] == ';' || uri[length] == ',');
}

bool tinta_svg_uri_candidate(const char *uri) {
    const char *end;
    const char *dot;
    if (!uri) return false;
    if (tinta_svg_data_uri(uri)) return true;
    end = uri + strcspn(uri, "?#");
    dot = end;
    while (dot > uri && dot[-1] != '/' && dot[-1] != '\\') {
        dot--;
        if (*dot == '.')
            return (size_t)(end - dot) == 4 &&
                   ascii_equal_n(dot, ".svg", 4);
    }
    return false;
}

static bool parse_number(const char *text, size_t length, float *value) {
    char buffer[96];
    char *end;
    double parsed;
    if (!length || length >= sizeof(buffer)) return false;
    memcpy(buffer, text, length);
    buffer[length] = 0;
    parsed = strtod(buffer, &end);
    if (end == buffer || !isfinite(parsed) || parsed <= 0) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) {
        double factor = 1.0;
        if (!_stricmp(end, "px")) factor = 1.0;
        else if (!_stricmp(end, "pt")) factor = 96.0 / 72.0;
        else if (!_stricmp(end, "pc")) factor = 16.0;
        else if (!_stricmp(end, "in")) factor = 96.0;
        else if (!_stricmp(end, "cm")) factor = 96.0 / 2.54;
        else if (!_stricmp(end, "mm")) factor = 96.0 / 25.4;
        else return false;
        parsed *= factor;
    }
    if (!isfinite(parsed) || parsed <= 0 || parsed > 1000000.0) return false;
    *value = (float)parsed;
    return true;
}

static bool attribute_value(const char *tag, size_t length, const char *name,
                            const char **value, size_t *value_length) {
    size_t name_length = strlen(name);
    size_t i = 0;
    while (i < length) {
        size_t start;
        size_t end_name;
        char quote;
        while (i < length && isspace((unsigned char)tag[i])) i++;
        start = i;
        while (i < length &&
               (isalnum((unsigned char)tag[i]) || tag[i] == '-' ||
                tag[i] == ':' || tag[i] == '_')) i++;
        if (i == start) { i++; continue; }
        end_name = i;
        while (i < length && isspace((unsigned char)tag[i])) i++;
        if (i >= length || tag[i] != '=') continue;
        i++;
        while (i < length && isspace((unsigned char)tag[i])) i++;
        if (i >= length || (tag[i] != '\'' && tag[i] != '"')) continue;
        quote = tag[i++];
        {
            size_t content = i;
            while (i < length && tag[i] != quote) i++;
            if (end_name - start == name_length &&
                ascii_equal_n(tag + start, name, name_length) &&
                i < length) {
                *value = tag + content;
                *value_length = i - content;
                return true;
            }
            if (i < length) i++;
        }
    }
    return false;
}

static bool parse_view_box(const char *text, size_t length,
                           float *width, float *height) {
    char buffer[192];
    char *cursor;
    char *end;
    double values[4];
    int i;
    if (!length || length >= sizeof(buffer)) return false;
    memcpy(buffer, text, length);
    buffer[length] = 0;
    cursor = buffer;
    for (i = 0; i < 4; i++) {
        while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ','))
            cursor++;
        values[i] = strtod(cursor, &end);
        if (end == cursor || !isfinite(values[i])) return false;
        cursor = end;
    }
    if (values[2] <= 0 || values[3] <= 0) return false;
    *width = (float)values[2];
    *height = (float)values[3];
    return true;
}

bool tinta_svg_prepare_source(const char *data, size_t length,
                              size_t maximum_bytes, TintaSvgInfo *info) {
    const char *svg;
    const char *tag_end;
    const char *value;
    size_t value_length;
    float width = 0;
    float height = 0;
    float view_width = 0;
    float view_height = 0;
    bool has_width;
    bool has_height;
    if (!data || !length || length > INT_MAX || !info ||
        (maximum_bytes && length > maximum_bytes) ||
        memchr(data, 0, length) ||
        ascii_find_nocase(data, length, "<!doctype") ||
        ascii_find_nocase(data, length, "<!entity") ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data,
                            (int)length, NULL, 0) <= 0) {
        return false;
    }
    svg = ascii_find_nocase(data, length, "<svg");
    while (svg && svg + 4 < data + length &&
           !isspace((unsigned char)svg[4]) && svg[4] != '>') {
        size_t remaining = length - (size_t)(svg + 4 - data);
        const char *next = ascii_find_nocase(svg + 4, remaining, "<svg");
        svg = next;
    }
    if (!svg || svg + 4 >= data + length) return false;
    tag_end = memchr(svg, '>', length - (size_t)(svg - data));
    if (!tag_end) return false;
    has_width = attribute_value(svg + 4, (size_t)(tag_end - svg - 4),
                                "width", &value, &value_length) &&
                parse_number(value, value_length, &width);
    has_height = attribute_value(svg + 4, (size_t)(tag_end - svg - 4),
                                 "height", &value, &value_length) &&
                 parse_number(value, value_length, &height);
    if (attribute_value(svg + 4, (size_t)(tag_end - svg - 4),
                        "viewBox", &value, &value_length))
        parse_view_box(value, value_length, &view_width, &view_height);
    if (!has_width && !has_height) {
        if (view_width > 0 && view_height > 0) {
            width = view_width;
            height = view_height;
        } else {
            width = 300;
            height = 150;
        }
    } else if (!has_width) {
        width = view_width > 0 && view_height > 0 ?
            height * view_width / view_height : 300;
    } else if (!has_height) {
        height = view_width > 0 && view_height > 0 ?
            width * view_height / view_width : 150;
    }
    info->width = width;
    info->height = height;
    return width > 0 && height > 0;
}

static int base64_value(unsigned char value) {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

static bool decode_base64(const char *text, size_t length, size_t maximum,
                          TintaStr8 *output) {
    unsigned int accumulator = 0;
    int bits = 0;
    size_t i;
    bool padded = false;
    for (i = 0; i < length; i++) {
        int value;
        if (isspace((unsigned char)text[i])) continue;
        if (text[i] == '=') {
            padded = true;
            continue;
        }
        if (padded) return false;
        value = base64_value((unsigned char)text[i]);
        if (value < 0) return false;
        accumulator = (accumulator << 6) | (unsigned int)value;
        bits += 6;
        if (bits >= 8) {
            char byte;
            bits -= 8;
            byte = (char)((accumulator >> bits) & 0xff);
            if ((maximum && output->len >= maximum) ||
                !tinta_str8_append_char(output, byte)) return false;
        }
    }
    return true;
}

static int hex_value(unsigned char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool tinta_svg_decode_data_uri(const char *uri, size_t maximum_bytes,
                               TintaStr8 *source, TintaSvgInfo *info) {
    const char *comma;
    const char *metadata;
    size_t metadata_length;
    bool base64 = false;
    TintaStr8 decoded = {0};
    size_t i;
    if (!tinta_svg_data_uri(uri) || !source || !info) return false;
    comma = strchr(uri, ',');
    if (!comma) return false;
    metadata = uri + strlen("data:image/svg+xml");
    metadata_length = (size_t)(comma - metadata);
    if (metadata_length) {
        i = 0;
        while (i < metadata_length) {
            size_t start;
            size_t end;
            if (metadata[i] != ';') return false;
            start = ++i;
            while (i < metadata_length && metadata[i] != ';') i++;
            end = i;
            if (end == start) return false;
            if (end - start == 6 &&
                ascii_equal_n(metadata + start, "base64", 6)) {
                base64 = true;
            } else if (!(end - start == 13 &&
                         ascii_equal_n(metadata + start,
                                       "charset=utf-8", 13))) {
                return false;
            }
        }
    }
    if (base64) {
        if (!decode_base64(comma + 1, strlen(comma + 1),
                           maximum_bytes, &decoded)) goto failed;
    } else {
        const char *text = comma + 1;
        for (i = 0; text[i]; i++) {
            char byte = text[i];
            if (byte == '%') {
                int high;
                int low;
                if (!text[i + 1] || !text[i + 2]) goto failed;
                high = hex_value((unsigned char)text[i + 1]);
                low = hex_value((unsigned char)text[i + 2]);
                if (high < 0 || low < 0) goto failed;
                byte = (char)((high << 4) | low);
                i += 2;
            }
            if ((maximum_bytes && decoded.len >= maximum_bytes) ||
                !tinta_str8_append_char(&decoded, byte)) goto failed;
        }
    }
    if (!tinta_svg_prepare_source(decoded.data, decoded.len,
                                  maximum_bytes, info)) goto failed;
    tinta_str8_destroy(source);
    *source = decoded;
    return true;
failed:
    tinta_str8_destroy(&decoded);
    return false;
}
