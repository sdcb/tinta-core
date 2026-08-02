#ifndef TINTA_DOCUMENT_H
#define TINTA_DOCUMENT_H

#include "markdown.h"

#ifdef __cplusplus
extern "C" {
#endif

bool tinta_is_mermaid_document_path_utf8(const char *path);
bool tinta_is_mermaid_document_path_utf16(const wchar_t *path);
bool tinta_is_supported_document_path_utf8(const char *path);
bool tinta_is_supported_document_path_utf16(const wchar_t *path);
bool tinta_is_supported_drop_path_utf16(const wchar_t *path);

TintaParseResult tinta_parse_document(const char *content, size_t length,
                                      const char *path,
                                      const TintaMarkdownOptions *options);

#ifdef __cplusplus
}
#endif

#endif

