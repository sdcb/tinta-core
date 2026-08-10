#include "test_harness.h"
#include "syntax.h"

#include <cwchar>

void test_syntax_languages(TintaTestContext &tests) {
    tests.check(tinta_syntax_language("cpp") == 1 &&
                    tinta_syntax_language("python") == 2 &&
                    tinta_syntax_language("typescript") == 3 &&
                    tinta_syntax_language("c#") == 7,
                "language detection");
}

void test_syntax_token_classes(TintaTestContext &tests) {
    const wchar_t *line = L"if (value == 42) return \"ok\"; // comment";
    TintaVec tokens{};
    const bool initialized = tinta_vec_init(&tokens, sizeof(TintaSyntaxToken));
    tests.check(initialized, "token vector initialization");
    if (!initialized) return;

    bool block = false;
    const bool tokenized = tinta_syntax_tokenize(line, std::wcslen(line), 1,
                                                 &block, &tokens);
    tests.check(tokenized, "tokenization");
    if (tokenized) {
        bool control = false;
        bool number = false;
        bool string = false;
        bool comment = false;
        for (size_t index = 0; index < tokens.len; index++) {
            const auto token = TINTA_VEC_AT(TintaSyntaxToken, tokens, index);
            control |= token.type == TINTA_SYNTAX_CONTROL;
            number |= token.type == TINTA_SYNTAX_NUMBER;
            string |= token.type == TINTA_SYNTAX_STRING;
            comment |= token.type == TINTA_SYNTAX_COMMENT;
        }
        tests.check(control && number && string && comment,
                    "token classes");
    }
    tinta_vec_destroy(&tokens);
}

void test_syntax_type_and_function(TintaTestContext &tests) {
    const wchar_t *line = L"size_t count = parse();";
    TintaVec tokens{};
    const bool initialized = tinta_vec_init(&tokens, sizeof(TintaSyntaxToken));
    tests.check(initialized, "type token vector initialization");
    if (!initialized) return;

    bool block = false;
    const bool tokenized = tinta_syntax_tokenize(line, std::wcslen(line), 1,
                                                 &block, &tokens);
    tests.check(tokenized, "type tokenization");
    if (tokenized) {
        bool type = false;
        bool function = false;
        for (size_t index = 0; index < tokens.len; index++) {
            const auto token = TINTA_VEC_AT(TintaSyntaxToken, tokens, index);
            type |= token.type == TINTA_SYNTAX_TYPE;
            function |= token.type == TINTA_SYNTAX_FUNCTION;
        }
        tests.check(type && function, "type and function classification");
    }
    tinta_vec_destroy(&tokens);
}
