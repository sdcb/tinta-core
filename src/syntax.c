#include "syntax.h"

#include <string.h>
#include <wctype.h>

static const wchar_t *CPP_WORDS[] = {L"if",L"else",L"for",L"while",L"switch",L"case",L"break",L"continue",L"return",L"void",L"int",L"char",L"float",L"double",L"bool",L"long",L"short",L"unsigned",L"const",L"static",L"extern",L"class",L"struct",L"enum",L"typedef",L"template",L"namespace",L"public",L"private",L"protected",L"virtual",L"override",L"auto",L"new",L"delete",L"true",L"false",L"nullptr",L"try",L"catch",L"throw",L"using",L"sizeof"};
static const wchar_t *PY_WORDS[] = {L"if",L"elif",L"else",L"for",L"while",L"break",L"continue",L"return",L"def",L"class",L"import",L"from",L"as",L"try",L"except",L"finally",L"raise",L"with",L"yield",L"lambda",L"in",L"is",L"not",L"and",L"or",L"True",L"False",L"None",L"async",L"await",L"match",L"case"};
static const wchar_t *JS_WORDS[] = {L"if",L"else",L"for",L"while",L"switch",L"case",L"break",L"continue",L"return",L"function",L"var",L"let",L"const",L"class",L"extends",L"new",L"this",L"try",L"catch",L"throw",L"async",L"await",L"import",L"export",L"true",L"false",L"null",L"undefined"};
static const wchar_t *RUST_WORDS[] = {L"if",L"else",L"match",L"for",L"while",L"loop",L"break",L"continue",L"return",L"fn",L"let",L"mut",L"const",L"static",L"struct",L"enum",L"trait",L"impl",L"pub",L"mod",L"use",L"crate",L"self",L"where",L"as",L"in",L"type",L"unsafe",L"async",L"await",L"true",L"false"};
static const wchar_t *GO_WORDS[] = {L"if",L"else",L"for",L"range",L"switch",L"case",L"break",L"continue",L"return",L"func",L"var",L"const",L"type",L"struct",L"interface",L"map",L"chan",L"package",L"import",L"go",L"defer",L"select",L"default",L"true",L"false",L"nil"};
static const wchar_t *BASH_WORDS[] = {L"if",L"then",L"else",L"elif",L"fi",L"for",L"in",L"do",L"done",L"while",L"until",L"case",L"esac",L"function",L"return",L"local",L"export",L"source",L"set",L"unset",L"exit",L"break",L"continue",L"echo",L"printf",L"read",L"cd",L"pwd"};
static const wchar_t *CS_WORDS[] = {L"if",L"else",L"for",L"foreach",L"while",L"switch",L"case",L"break",L"continue",L"return",L"throw",L"try",L"catch",L"finally",L"void",L"int",L"char",L"float",L"double",L"bool",L"long",L"string",L"object",L"var",L"public",L"private",L"protected",L"internal",L"static",L"const",L"readonly",L"abstract",L"virtual",L"override",L"class",L"struct",L"interface",L"enum",L"delegate",L"record",L"namespace",L"new",L"this",L"base",L"using",L"in",L"out",L"ref",L"is",L"as",L"typeof",L"await",L"async",L"true",L"false",L"null"};
static const wchar_t *CONTROL_WORDS[] = {
    L"if", L"else", L"elif", L"for", L"foreach", L"while", L"until",
    L"loop", L"switch", L"case", L"match", L"break", L"continue",
    L"return", L"throw", L"try", L"catch", L"except", L"finally",
    L"yield", L"await"
};
static const wchar_t *CPP_TYPES[] = {
    L"size_t", L"int8_t", L"int16_t", L"int32_t", L"int64_t",
    L"uint8_t", L"uint16_t", L"uint32_t", L"uint64_t", L"string",
    L"wstring", L"vector", L"map", L"set", L"unordered_map",
    L"shared_ptr", L"unique_ptr", L"optional", L"variant", L"HRESULT",
    L"HWND", L"HINSTANCE", L"LPARAM", L"WPARAM", L"LRESULT", L"DWORD"
};
static const wchar_t *CS_TYPES[] = {
    L"String", L"Int32", L"Int64", L"Boolean", L"Double", L"Decimal",
    L"Object", L"Guid", L"DateTime", L"List", L"Dictionary", L"HashSet",
    L"IEnumerable", L"IDisposable", L"Task", L"ValueTask", L"Span"
};

static bool ascii_eq(const char *left, const char *right) {
    while (*left && *right) {
        char a = *left++, b = *right++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return !*left && !*right;
}

int tinta_syntax_language(const char *name) {
    if (!name) return 0;
    if (ascii_eq(name, "c") || ascii_eq(name, "cpp") ||
        ascii_eq(name, "c++") || ascii_eq(name, "h") ||
        ascii_eq(name, "hpp") || ascii_eq(name, "cxx")) return 1;
    if (ascii_eq(name, "python") || ascii_eq(name, "py")) return 2;
    if (ascii_eq(name, "javascript") || ascii_eq(name, "js") ||
        ascii_eq(name, "jsx") || ascii_eq(name, "typescript") ||
        ascii_eq(name, "ts") || ascii_eq(name, "tsx")) return 3;
    if (ascii_eq(name, "rust") || ascii_eq(name, "rs")) return 4;
    if (ascii_eq(name, "go") || ascii_eq(name, "golang")) return 5;
    if (ascii_eq(name, "bash") || ascii_eq(name, "sh") ||
        ascii_eq(name, "shell") || ascii_eq(name, "zsh")) return 6;
    if (ascii_eq(name, "csharp") || ascii_eq(name, "cs") ||
        ascii_eq(name, "c#")) return 7;
    return 0;
}

static bool word_eq(const wchar_t *text, size_t length, const wchar_t *word) {
    return wcslen(word) == length && wmemcmp(text, word, length) == 0;
}

static bool word_in(const wchar_t *text, size_t length,
                    const wchar_t *const *words, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (word_eq(text, length, words[i])) return true;
    }
    return false;
}

static TintaSyntaxType identifier_type(const wchar_t *text, size_t length,
                                       int language) {
    const wchar_t *const *words = NULL;
    size_t count = 0;
    if (word_in(text, length, CONTROL_WORDS,
                sizeof(CONTROL_WORDS) / sizeof(*CONTROL_WORDS)))
        return TINTA_SYNTAX_CONTROL;
    switch (language) {
        case 1: words = CPP_WORDS; count = sizeof(CPP_WORDS) / sizeof(*CPP_WORDS); break;
        case 2: words = PY_WORDS; count = sizeof(PY_WORDS) / sizeof(*PY_WORDS); break;
        case 3: words = JS_WORDS; count = sizeof(JS_WORDS) / sizeof(*JS_WORDS); break;
        case 4: words = RUST_WORDS; count = sizeof(RUST_WORDS) / sizeof(*RUST_WORDS); break;
        case 5: words = GO_WORDS; count = sizeof(GO_WORDS) / sizeof(*GO_WORDS); break;
        case 6: words = BASH_WORDS; count = sizeof(BASH_WORDS) / sizeof(*BASH_WORDS); break;
        case 7: words = CS_WORDS; count = sizeof(CS_WORDS) / sizeof(*CS_WORDS); break;
        default: break;
    }
    if (word_in(text, length, words, count)) return TINTA_SYNTAX_KEYWORD;
    if (language == 1 && word_in(text, length, CPP_TYPES,
                                 sizeof(CPP_TYPES) / sizeof(*CPP_TYPES)))
        return TINTA_SYNTAX_TYPE;
    if (language == 7 && word_in(text, length, CS_TYPES,
                                 sizeof(CS_TYPES) / sizeof(*CS_TYPES)))
        return TINTA_SYNTAX_TYPE;
    return TINTA_SYNTAX_PLAIN;
}

static bool push(TintaVec *tokens, size_t start, size_t length,
                 TintaSyntaxType type) {
    TintaSyntaxToken token = {start, length, type};
    return tinta_vec_push(tokens, &token) != NULL;
}

bool tinta_syntax_tokenize(const wchar_t *line, size_t length, int language,
                           bool *block, TintaVec *tokens) {
    size_t i = 0;
    if (!line || !block || !tokens) return false;
    tinta_vec_clear(tokens);
    while (i < length) {
        size_t start = i;
        if (*block) {
            while (i + 1 < length &&
                   !(line[i] == L'*' && line[i + 1] == L'/')) i++;
            if (i + 1 < length) {
                i += 2;
                *block = false;
            } else {
                i = length;
            }
            if (!push(tokens, start, i - start, TINTA_SYNTAX_COMMENT))
                return false;
            continue;
        }
        if (iswspace(line[i])) {
            while (i < length && iswspace(line[i])) i++;
            if (!push(tokens, start, i - start, TINTA_SYNTAX_PLAIN))
                return false;
            continue;
        }
        if (i + 1 < length && line[i] == L'/' && line[i + 1] == L'/')
            return push(tokens, i, length - i, TINTA_SYNTAX_COMMENT);
        if ((language == 2 || language == 6) && line[i] == L'#')
            return push(tokens, i, length - i, TINTA_SYNTAX_COMMENT);
        if ((language == 1 || language == 7) && line[i] == L'#')
            return push(tokens, i, length - i, TINTA_SYNTAX_KEYWORD);
        if (i + 1 < length && line[i] == L'/' && line[i + 1] == L'*') {
            i += 2;
            while (i + 1 < length &&
                   !(line[i] == L'*' && line[i + 1] == L'/')) i++;
            if (i + 1 < length) i += 2;
            else {
                *block = true;
                i = length;
            }
            if (!push(tokens, start, i - start, TINTA_SYNTAX_COMMENT))
                return false;
            continue;
        }
        if (line[i] == L'"' || line[i] == L'\'' ||
            (language == 3 && line[i] == L'`')) {
            wchar_t quote = line[i++];
            while (i < length) {
                if (line[i] == L'\\' && i + 1 < length) i += 2;
                else if (line[i++] == quote) break;
            }
            if (!push(tokens, start, i - start, TINTA_SYNTAX_STRING))
                return false;
            continue;
        }
        if (iswdigit(line[i]) ||
            (line[i] == L'.' && i + 1 < length && iswdigit(line[i + 1]))) {
            i++;
            while (i < length &&
                   (iswalnum(line[i]) || line[i] == L'.' || line[i] == L'_'))
                i++;
            if (!push(tokens, start, i - start, TINTA_SYNTAX_NUMBER))
                return false;
            continue;
        }
        if (iswalpha(line[i]) || line[i] == L'_') {
            TintaSyntaxType type;
            size_t next;
            i++;
            while (i < length && (iswalnum(line[i]) || line[i] == L'_')) i++;
            type = identifier_type(line + start, i - start, language);
            next = i;
            while (next < length && iswspace(line[next])) next++;
            if (type == TINTA_SYNTAX_PLAIN && next < length &&
                line[next] == L'(')
                type = TINTA_SYNTAX_FUNCTION;
            else if (type == TINTA_SYNTAX_PLAIN && iswupper(line[start]) &&
                     i - start > 1)
                type = TINTA_SYNTAX_TYPE;
            if (!push(tokens, start, i - start, type)) return false;
            continue;
        }
        i++;
        if (!push(tokens, start, 1, TINTA_SYNTAX_OPERATOR)) return false;
    }
    return true;
}
