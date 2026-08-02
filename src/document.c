#include "document.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static bool extension_eq8(const char *path, const char *extension) {
    size_t path_len, ext_len, i;
    if (!path || !extension) return false;
    path_len = strlen(path);
    ext_len = strlen(extension);
    if (path_len < ext_len) return false;
    for (i = 0; i < ext_len; i++) {
        unsigned char a = (unsigned char)path[path_len - ext_len + i];
        unsigned char b = (unsigned char)extension[i];
        if (tolower(a) != tolower(b)) return false;
    }
    return true;
}

static bool extension_eq16(const wchar_t *path, const wchar_t *extension) {
    size_t path_len, ext_len, i;
    if (!path || !extension) return false;
    path_len = wcslen(path);
    ext_len = wcslen(extension);
    if (path_len < ext_len) return false;
    for (i = 0; i < ext_len; i++)
        if (towlower(path[path_len - ext_len + i]) != towlower(extension[i])) return false;
    return true;
}

bool tinta_is_mermaid_document_path_utf8(const char *path) {
    return extension_eq8(path, ".mmd");
}

bool tinta_is_mermaid_document_path_utf16(const wchar_t *path) {
    return extension_eq16(path, L".mmd");
}

bool tinta_is_supported_document_path_utf8(const char *path) {
    return extension_eq8(path, ".md") || extension_eq8(path, ".markdown") ||
           extension_eq8(path, ".mmd");
}

bool tinta_is_supported_document_path_utf16(const wchar_t *path) {
    return extension_eq16(path, L".md") || extension_eq16(path, L".markdown") ||
           extension_eq16(path, L".mmd");
}

bool tinta_is_supported_drop_path_utf16(const wchar_t *path) {
    return tinta_is_supported_document_path_utf16(path) || extension_eq16(path, L".txt");
}

TintaParseResult tinta_parse_document(const char *content, size_t length,
                                      const char *path,
                                      const TintaMarkdownOptions *options) {
    TintaParseResult result;
    if (!tinta_is_mermaid_document_path_utf8(path))
        return tinta_markdown_parse(content, length, options);
    memset(&result, 0, sizeof(result));
    result.root = tinta_element_create(TINTA_ELEMENT_DOCUMENT);
    if (result.root) {
        TintaElement *diagram = tinta_element_create(TINTA_ELEMENT_MERMAID_DIAGRAM);
        if (diagram) {
            diagram->text = tinta_str8_dup(content ? content : "", length);
            diagram->source_offset = 0;
            if (diagram->text && tinta_element_add_child(result.root, diagram)) {
                result.success = true;
                return result;
            }
            tinta_element_destroy(diagram);
        }
    }
    tinta_element_destroy(result.root);
    result.root = NULL;
    result.error = tinta_str8_dup("Out of memory", 13);
    return result;
}

