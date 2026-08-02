#ifndef TINTA_SYNTAX_H
#define TINTA_SYNTAX_H

#include "tinta_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TintaSyntaxType {
    TINTA_SYNTAX_PLAIN,
    TINTA_SYNTAX_KEYWORD,
    TINTA_SYNTAX_STRING,
    TINTA_SYNTAX_COMMENT,
    TINTA_SYNTAX_NUMBER,
    TINTA_SYNTAX_FUNCTION,
    TINTA_SYNTAX_TYPE,
    TINTA_SYNTAX_OPERATOR,
    TINTA_SYNTAX_CONTROL
} TintaSyntaxType;

typedef struct TintaSyntaxToken {
    size_t start;
    size_t length;
    TintaSyntaxType type;
} TintaSyntaxToken;

int tinta_syntax_language(const char *name);
bool tinta_syntax_tokenize(const wchar_t *line, size_t length, int language,
                           bool *block_comment, TintaVec *tokens);

#ifdef __cplusplus
}
#endif

#endif

