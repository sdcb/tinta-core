#include "syntax.h"

#include <iostream>

int main() {
    if (tinta_syntax_language("cpp") != 1 || tinta_syntax_language("python") != 2 ||
        tinta_syntax_language("typescript") != 3 || tinta_syntax_language("c#") != 7) {
        std::cerr << "language detection failed\n";
        return 1;
    }
    const wchar_t *line = L"if (value == 42) return \"ok\"; // comment";
    TintaVec tokens{};
    tinta_vec_init(&tokens, sizeof(TintaSyntaxToken));
    bool block = false;
    if (!tinta_syntax_tokenize(line, wcslen(line), 1, &block, &tokens)) return 1;
    bool control = false, number = false, string = false, comment = false;
    for (size_t i = 0; i < tokens.len; i++) {
        auto token = TINTA_VEC_AT(TintaSyntaxToken, tokens, i);
        control |= token.type == TINTA_SYNTAX_CONTROL;
        number |= token.type == TINTA_SYNTAX_NUMBER;
        string |= token.type == TINTA_SYNTAX_STRING;
        comment |= token.type == TINTA_SYNTAX_COMMENT;
    }
    tinta_vec_destroy(&tokens);
    if (!control || !number || !string || !comment) {
        std::cerr << "token classes missing\n";
        return 1;
    }

    tinta_vec_init(&tokens, sizeof(TintaSyntaxToken));
    block = false;
    const wchar_t *types = L"size_t count = parse();";
    if (!tinta_syntax_tokenize(types, wcslen(types), 1, &block, &tokens))
        return 1;
    bool type = false, function = false;
    for (size_t i = 0; i < tokens.len; i++) {
        auto token = TINTA_VEC_AT(TintaSyntaxToken, tokens, i);
        type |= token.type == TINTA_SYNTAX_TYPE;
        function |= token.type == TINTA_SYNTAX_FUNCTION;
    }
    tinta_vec_destroy(&tokens);
    if (!type || !function) {
        std::cerr << "type or function classification missing\n";
        return 1;
    }
    std::cout << "Syntax tests passed\n";
    return 0;
}
