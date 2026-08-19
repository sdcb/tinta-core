#include "tinta_math.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct MathParser {
    const char *source;
    size_t length;
    size_t position;
    size_t node_count;
    size_t max_nodes;
    size_t max_depth;
    size_t error_offset;
    bool suppress_scripts;
    bool failed;
} MathParser;

typedef struct MathSymbol {
    const char *command;
    const char *text;
    bool large;
} MathSymbol;

static const MathSymbol MATH_SYMBOLS[] = {
    {"alpha", "\xCE\xB1", false}, {"beta", "\xCE\xB2", false},
    {"gamma", "\xCE\xB3", false}, {"delta", "\xCE\xB4", false},
    {"epsilon", "\xCF\xB5", false}, {"varepsilon", "\xCE\xB5", false},
    {"zeta", "\xCE\xB6", false}, {"eta", "\xCE\xB7", false},
    {"theta", "\xCE\xB8", false}, {"vartheta", "\xCF\x91", false},
    {"iota", "\xCE\xB9", false}, {"kappa", "\xCE\xBA", false},
    {"lambda", "\xCE\xBB", false}, {"mu", "\xCE\xBC", false},
    {"nu", "\xCE\xBD", false}, {"xi", "\xCE\xBE", false},
    {"pi", "\xCF\x80", false}, {"varpi", "\xCF\x96", false},
    {"rho", "\xCF\x81", false}, {"varrho", "\xCF\xB1", false},
    {"sigma", "\xCF\x83", false}, {"varsigma", "\xCF\x82", false},
    {"tau", "\xCF\x84", false}, {"upsilon", "\xCF\x85", false},
    {"phi", "\xCF\x86", false}, {"varphi", "\xCF\x95", false},
    {"chi", "\xCF\x87", false}, {"psi", "\xCF\x88", false},
    {"omega", "\xCF\x89", false}, {"Gamma", "\xCE\x93", false},
    {"Delta", "\xCE\x94", false}, {"Theta", "\xCE\x98", false},
    {"Lambda", "\xCE\x9B", false}, {"Xi", "\xCE\x9E", false},
    {"Pi", "\xCE\xA0", false}, {"Sigma", "\xCE\xA3", false},
    {"Upsilon", "\xCE\xA5", false}, {"Phi", "\xCE\xA6", false},
    {"Psi", "\xCE\xA8", false}, {"Omega", "\xCE\xA9", false},
    {"times", "\xC3\x97", false}, {"div", "\xC3\xB7", false},
    {"cdot", "\xE2\x8B\x85", false}, {"pm", "\xC2\xB1", false},
    {"mp", "\xE2\x88\x93", false}, {"ast", "\xE2\x88\x97", false},
    {"star", "\xE2\x8B\x86", false}, {"circ", "\xE2\x88\x98", false},
    {"bullet", "\xE2\x80\xA2", false}, {"oplus", "\xE2\x8A\x95", false},
    {"otimes", "\xE2\x8A\x97", false}, {"cap", "\xE2\x88\xA9", false},
    {"cup", "\xE2\x88\xAA", false}, {"wedge", "\xE2\x88\xA7", false},
    {"vee", "\xE2\x88\xA8", false}, {"le", "\xE2\x89\xA4", false},
    {"leq", "\xE2\x89\xA4", false}, {"ge", "\xE2\x89\xA5", false},
    {"geq", "\xE2\x89\xA5", false}, {"ne", "\xE2\x89\xA0", false},
    {"neq", "\xE2\x89\xA0", false}, {"approx", "\xE2\x89\x88", false},
    {"sim", "\xE2\x88\xBC", false}, {"equiv", "\xE2\x89\xA1", false},
    {"propto", "\xE2\x88\x9D", false}, {"in", "\xE2\x88\x88", false},
    {"notin", "\xE2\x88\x89", false}, {"ni", "\xE2\x88\x8B", false},
    {"subset", "\xE2\x8A\x82", false}, {"supset", "\xE2\x8A\x83", false},
    {"subseteq", "\xE2\x8A\x86", false}, {"supseteq", "\xE2\x8A\x87", false},
    {"perp", "\xE2\x9F\x82", false}, {"parallel", "\xE2\x88\xA5", false},
    {"to", "\xE2\x86\x92", false}, {"rightarrow", "\xE2\x86\x92", false},
    {"leftarrow", "\xE2\x86\x90", false}, {"leftrightarrow", "\xE2\x86\x94", false},
    {"Rightarrow", "\xE2\x87\x92", false}, {"Leftarrow", "\xE2\x87\x90", false},
    {"Leftrightarrow", "\xE2\x87\x94", false}, {"mapsto", "\xE2\x86\xA6", false},
    {"cdots", "\xE2\x8B\xAF", false}, {"ldots", "\xE2\x80\xA6", false},
    {"dots", "\xE2\x80\xA6", false}, {"vdots", "\xE2\x8B\xAE", false},
    {"ddots", "\xE2\x8B\xB1", false}, {"mid", "\xE2\x88\xA3", false},
    {"varnothing", "\xE2\x88\x85", false}, {"because", "\xE2\x88\xB5", false},
    {"therefore", "\xE2\x88\xB4", false}, {"bigtriangleup", "\xE2\x96\xB3", false},
    {"square", "\xE2\x96\xA1", false}, {"prime", "\xE2\x80\xB2", false},
    {"degree", "\xC2\xB0", false}, {"lfloor", "\xE2\x8C\x8A", false},
    {"rfloor", "\xE2\x8C\x8B", false}, {"lceil", "\xE2\x8C\x88", false},
    {"rceil", "\xE2\x8C\x89", false}, {"langle", "\xE2\x9F\xA8", false},
    {"rangle", "\xE2\x9F\xA9", false}, {"lbrack", "[", false},
    {"rbrack", "]", false}, {"lvert", "|", false}, {"rvert", "|", false},
    {"vert", "|", false}, {"lVert", "\xE2\x80\x96", false},
    {"rVert", "\xE2\x80\x96", false}, {"Vert", "\xE2\x80\x96", false},
    {"backslash", "\\", false}, {"setminus", "\xE2\x88\x96", false},
    {"hbar", "\xE2\x84\x8F", false}, {"Re", "\xE2\x84\x9C", false},
    {"Im", "\xE2\x84\x91", false}, {"aleph", "\xE2\x84\xB5", false},
    {"wp", "\xE2\x84\x98", false}, {"lnot", "\xC2\xAC", false},
    {"land", "\xE2\x88\xA7", false}, {"lor", "\xE2\x88\xA8", false},
    {"infty", "\xE2\x88\x9E", false}, {"partial", "\xE2\x88\x82", false},
    {"nabla", "\xE2\x88\x87", false}, {"ell", "\xE2\x84\x93", false},
    {"emptyset", "\xE2\x88\x85", false}, {"forall", "\xE2\x88\x80", false},
    {"exists", "\xE2\x88\x83", false}, {"neg", "\xC2\xAC", false},
    {"angle", "\xE2\x88\xA0", false}, {"triangle", "\xE2\x96\xB3", false},
    {"sum", "\xE2\x88\x91", true}, {"prod", "\xE2\x88\x8F", true},
    {"coprod", "\xE2\x88\x90", true}, {"int", "\xE2\x88\xAB", true},
    {"iint", "\xE2\x88\xAC", true}, {"iiint", "\xE2\x88\xAD", true},
    {"oint", "\xE2\x88\xAE", true}, {"lim", "lim", true},
    {"min", "min", true}, {"max", "max", true}, {"log", "log", false},
    {"ln", "ln", false}, {"lg", "lg", false}, {"sin", "sin", false},
    {"cos", "cos", false}, {"tan", "tan", false}, {"cot", "cot", false},
    {"sec", "sec", false}, {"csc", "csc", false}, {"sinh", "sinh", false},
    {"cosh", "cosh", false}, {"tanh", "tanh", false},
    {"limsup", "limsup", true}, {"liminf", "liminf", true},
    {"sup", "sup", true}, {"inf", "inf", true}, {"exp", "exp", false},
    {"mod", "mod", false}, {"Pr", "Pr", false}, {"dim", "dim", false},
    {"ker", "ker", false}, {"deg", "deg", false}, {"arg", "arg", false},
    {"det", "det", false}, {"gcd", "gcd", false}
};

static void parser_fail(MathParser *parser) {
    if (!parser->failed) parser->error_offset = parser->position;
    parser->failed = true;
}

static char *duplicate_bytes(const char *text, size_t length) {
    char *copy = (char *)malloc(length + 1);
    if (!copy) return NULL;
    if (length) memcpy(copy, text, length);
    copy[length] = 0;
    return copy;
}

static TintaMathNode *node_create(MathParser *parser, TintaMathNodeType type) {
    TintaMathNode *node;
    if (parser->failed ||
        (parser->max_nodes && parser->node_count >= parser->max_nodes)) {
        parser_fail(parser);
        return NULL;
    }
    node = (TintaMathNode *)calloc(1, sizeof(*node));
    if (!node) {
        parser_fail(parser);
        return NULL;
    }
    node->type = type;
    node->style = TINTA_MATH_STYLE_DEFAULT;
    node->limits_mode = TINTA_MATH_LIMITS_AUTO;
    parser->node_count++;
    return node;
}

void tinta_math_node_destroy(TintaMathNode *node) {
    size_t i;
    if (!node) return;
    tinta_math_node_destroy(node->a);
    tinta_math_node_destroy(node->b);
    tinta_math_node_destroy(node->c);
    for (i = 0; i < node->child_count; i++)
        tinta_math_node_destroy(node->children[i]);
    free(node->children);
    free(node->text);
    free(node->aux);
    free(node);
}

static bool node_add_child(MathParser *parser, TintaMathNode *node,
                           TintaMathNode *child) {
    TintaMathNode **children;
    size_t capacity;
    if (!node || !child) return false;
    if (node->child_count == node->child_capacity) {
        capacity = node->child_capacity ? node->child_capacity * 2 : 4;
        children = (TintaMathNode **)realloc(
            node->children, capacity * sizeof(*children));
        if (!children) {
            parser_fail(parser);
            return false;
        }
        node->children = children;
        node->child_capacity = capacity;
    }
    node->children[node->child_count++] = child;
    return true;
}

static void skip_space(MathParser *parser) {
    while (parser->position < parser->length &&
           isspace((unsigned char)parser->source[parser->position]))
        parser->position++;
}

static bool command_at(const MathParser *parser, const char *command) {
    size_t length = strlen(command);
    size_t at = parser->position;
    if (at >= parser->length || parser->source[at] != '\\' ||
        length > parser->length - at - 1 ||
        memcmp(parser->source + at + 1, command, length) != 0)
        return false;
    return at + 1 + length == parser->length ||
        !isalpha((unsigned char)parser->source[at + 1 + length]);
}

static char *read_command(MathParser *parser) {
    size_t start;
    if (parser->position >= parser->length ||
        parser->source[parser->position] != '\\') {
        parser_fail(parser);
        return NULL;
    }
    parser->position++;
    start = parser->position;
    if (parser->position < parser->length &&
        isalpha((unsigned char)parser->source[parser->position])) {
        while (parser->position < parser->length &&
               isalpha((unsigned char)parser->source[parser->position]))
            parser->position++;
    } else if (parser->position < parser->length) {
        parser->position++;
    } else {
        parser_fail(parser);
        return NULL;
    }
    return duplicate_bytes(parser->source + start, parser->position - start);
}

static char *read_group_text(MathParser *parser) {
    size_t start;
    size_t depth = 1;
    skip_space(parser);
    if (parser->position >= parser->length ||
        parser->source[parser->position] != '{') {
        parser_fail(parser);
        return NULL;
    }
    parser->position++;
    start = parser->position;
    while (parser->position < parser->length) {
        char value = parser->source[parser->position];
        if (value == '\\' && parser->position + 1 < parser->length) {
            parser->position += 2;
            continue;
        }
        if (value == '{') depth++;
        else if (value == '}' && --depth == 0) {
            char *result = duplicate_bytes(
                parser->source + start, parser->position - start);
            parser->position++;
            if (!result) parser_fail(parser);
            return result;
        }
        parser->position++;
    }
    parser_fail(parser);
    return NULL;
}

static TintaMathNode *parse_sequence(MathParser *, size_t, const char *);

static TintaMathNode *parse_group(MathParser *parser, size_t depth) {
    TintaMathNode *node;
    skip_space(parser);
    if (parser->position >= parser->length ||
        parser->source[parser->position] != '{') {
        parser_fail(parser);
        return NULL;
    }
    parser->position++;
    node = parse_sequence(parser, depth + 1, NULL);
    if (!node || parser->position >= parser->length ||
        parser->source[parser->position] != '}') {
        tinta_math_node_destroy(node);
        parser_fail(parser);
        return NULL;
    }
    parser->position++;
    return node;
}

static TintaMathNode *parse_argument(MathParser *parser, size_t depth);

static TintaMathNode *make_text_node(MathParser *parser, const char *text,
                                     size_t length) {
    TintaMathNode *node = node_create(parser, TINTA_MATH_TEXT);
    if (!node) return NULL;
    node->text = duplicate_bytes(text, length);
    if (!node->text) {
        tinta_math_node_destroy(node);
        parser_fail(parser);
        return NULL;
    }
    return node;
}

static TintaMathNode *make_space_node(MathParser *parser, float em) {
    TintaMathNode *node = node_create(parser, TINTA_MATH_SPACE);
    if (node) node->space_em = em;
    return node;
}

static const MathSymbol *find_symbol(const char *command) {
    size_t i;
    for (i = 0; i < sizeof(MATH_SYMBOLS) / sizeof(MATH_SYMBOLS[0]); i++)
        if (strcmp(command, MATH_SYMBOLS[i].command) == 0)
            return &MATH_SYMBOLS[i];
    return NULL;
}

static char *read_delimiter(MathParser *parser) {
    char *command;
    const MathSymbol *symbol;
    skip_space(parser);
    if (parser->position >= parser->length) {
        parser_fail(parser);
        return NULL;
    }
    if (parser->source[parser->position] != '\\')
        return duplicate_bytes(parser->source + parser->position++, 1);
    command = read_command(parser);
    if (!command) return NULL;
    if (!strcmp(command, "lbrace")) {
        free(command); return duplicate_bytes("{", 1);
    }
    if (!strcmp(command, "rbrace")) {
        free(command); return duplicate_bytes("}", 1);
    }
    if (!strcmp(command, "langle")) {
        free(command); return duplicate_bytes("\xE2\x9F\xA8", 3);
    }
    if (!strcmp(command, "rangle")) {
        free(command); return duplicate_bytes("\xE2\x9F\xA9", 3);
    }
    if (!strcmp(command, "vert") || !strcmp(command, "mid")) {
        free(command); return duplicate_bytes("|", 1);
    }
    if (!strcmp(command, "Vert")) {
        free(command); return duplicate_bytes("\xE2\x80\x96", 3);
    }
    if (!strcmp(command, ".")) return command;
    symbol = find_symbol(command);
    if (symbol) {
        char *result = duplicate_bytes(symbol->text, strlen(symbol->text));
        free(command);
        return result;
    }
    free(command);
    parser_fail(parser);
    return NULL;
}

static TintaMathNode *parse_environment(MathParser *parser, size_t depth) {
    TintaMathNode *matrix = NULL;
    TintaVec cells;
    TintaVec row_counts;
    char *environment = read_group_text(parser);
    size_t columns = 0;
    size_t current_columns = 0;
    bool done = false;
    if (!environment) return NULL;
    if (strcmp(environment, "aligned") && strcmp(environment, "alignedat") &&
        strcmp(environment, "gathered") && strcmp(environment, "split") &&
        strcmp(environment, "multline") && strcmp(environment, "multlined") &&
        strcmp(environment, "matrix") && strcmp(environment, "pmatrix") &&
        strcmp(environment, "bmatrix") && strcmp(environment, "Bmatrix") &&
        strcmp(environment, "vmatrix") && strcmp(environment, "Vmatrix") &&
        strcmp(environment, "cases")) {
        free(environment);
        parser_fail(parser);
        return NULL;
    }
    if (!strcmp(environment, "alignedat")) {
        char *count = read_group_text(parser);
        free(count);
        if (parser->failed) { free(environment); return NULL; }
    }
    tinta_vec_init(&cells, sizeof(TintaMathNode *));
    tinta_vec_init(&row_counts, sizeof(size_t));
    while (!done && !parser->failed) {
        TintaMathNode *cell = parse_sequence(parser, depth + 1, environment);
        if (!cell || !tinta_vec_push(&cells, &cell)) {
            tinta_math_node_destroy(cell);
            parser_fail(parser);
            break;
        }
        current_columns++;
        if (parser->position < parser->length &&
            parser->source[parser->position] == '&') {
            parser->position++;
            continue;
        }
        if (parser->position + 1 < parser->length &&
            parser->source[parser->position] == '\\' &&
            parser->source[parser->position + 1] == '\\') {
            parser->position += 2;
            skip_space(parser);
            if (parser->position < parser->length &&
                parser->source[parser->position] == '[') {
                while (parser->position < parser->length &&
                       parser->source[parser->position] != ']')
                    parser->position++;
                if (parser->position < parser->length) parser->position++;
            }
        } else if (command_at(parser, "end")) {
            char *end_name;
            char *end_command = read_command(parser);
            free(end_command);
            end_name = read_group_text(parser);
            if (!end_name || strcmp(end_name, environment)) parser_fail(parser);
            free(end_name);
            done = true;
        } else {
            parser_fail(parser);
        }
        if (!tinta_vec_push(&row_counts, &current_columns)) parser_fail(parser);
        if (current_columns > columns) columns = current_columns;
        current_columns = 0;
    }
    if (!parser->failed && done && row_counts.len) {
        size_t row;
        size_t source_index = 0;
        matrix = node_create(parser, TINTA_MATH_MATRIX);
        if (matrix) {
            matrix->rows = row_counts.len;
            matrix->columns = columns;
            if (!strcmp(environment, "pmatrix")) {
                matrix->text = duplicate_bytes("(", 1);
                matrix->aux = duplicate_bytes(")", 1);
            } else if (!strcmp(environment, "bmatrix")) {
                matrix->text = duplicate_bytes("[", 1);
                matrix->aux = duplicate_bytes("]", 1);
            } else if (!strcmp(environment, "Bmatrix")) {
                matrix->text = duplicate_bytes("{", 1);
                matrix->aux = duplicate_bytes("}", 1);
            } else if (!strcmp(environment, "vmatrix")) {
                matrix->text = duplicate_bytes("|", 1);
                matrix->aux = duplicate_bytes("|", 1);
            } else if (!strcmp(environment, "Vmatrix")) {
                matrix->text = duplicate_bytes("\xE2\x80\x96", 3);
                matrix->aux = duplicate_bytes("\xE2\x80\x96", 3);
            } else if (!strcmp(environment, "cases")) {
                matrix->text = duplicate_bytes("{", 1);
                matrix->aux = duplicate_bytes(".", 1);
            }
            for (row = 0; matrix && row < row_counts.len; row++) {
                size_t count = TINTA_VEC_AT(size_t, row_counts, row);
                size_t column;
                for (column = 0; column < columns; column++) {
                    TintaMathNode *cell;
                    if (column < count) {
                        cell = TINTA_VEC_AT(TintaMathNode *, cells,
                                            source_index++);
                        TINTA_VEC_AT(TintaMathNode *, cells,
                                     source_index - 1) = NULL;
                    } else
                        cell = node_create(parser, TINTA_MATH_ROW);
                    if (!cell || !node_add_child(parser, matrix, cell)) {
                        tinta_math_node_destroy(cell);
                        tinta_math_node_destroy(matrix);
                        matrix = NULL;
                        break;
                    }
                }
            }
        }
    }
    while (cells.len)
        tinta_math_node_destroy(TINTA_VEC_AT(
            TintaMathNode *, cells, --cells.len));
    tinta_vec_destroy(&cells);
    tinta_vec_destroy(&row_counts);
    free(environment);
    return matrix;
}

static TintaMathNode *parse_command(MathParser *parser, size_t depth) {
    char *command = read_command(parser);
    const MathSymbol *symbol;
    TintaMathNode *node = NULL;
    if (!command) return NULL;
    symbol = find_symbol(command);
    if (symbol) {
        node = make_text_node(parser, symbol->text, strlen(symbol->text));
        if (node) node->large_operator = symbol->large;
    } else if (!strcmp(command, "frac") || !strcmp(command, "dfrac") ||
               !strcmp(command, "tfrac") || !strcmp(command, "binom")) {
        node = node_create(parser, !strcmp(command, "binom") ?
                           TINTA_MATH_DELIMITED : TINTA_MATH_FRACTION);
        if (node) {
            node->a = parse_group(parser, depth + 1);
            node->b = parse_group(parser, depth + 1);
            if (!strcmp(command, "dfrac")) node->style = TINTA_MATH_STYLE_DISPLAY;
            if (!strcmp(command, "tfrac")) node->style = TINTA_MATH_STYLE_SCRIPT;
            if (!strcmp(command, "binom")) {
                TintaMathNode *fraction = node_create(parser, TINTA_MATH_FRACTION);
                if (fraction) {
                    fraction->a = node->a; fraction->b = node->b;
                    fraction->space_em = -1.0f;
                    node->a = fraction; node->b = NULL;
                    node->text = duplicate_bytes("(", 1);
                    node->aux = duplicate_bytes(")", 1);
                }
            }
            if (!node->a || (!node->b && node->type == TINTA_MATH_FRACTION))
                parser_fail(parser);
        }
    } else if (!strcmp(command, "sqrt")) {
        node = node_create(parser, TINTA_MATH_ROOT);
        if (node) {
            skip_space(parser);
            if (parser->position < parser->length &&
                parser->source[parser->position] == '[') {
                parser->position++;
                node->b = parse_sequence(parser, depth + 1, "]");
                if (parser->position >= parser->length ||
                    parser->source[parser->position] != ']') parser_fail(parser);
                else parser->position++;
            }
            node->a = parse_group(parser, depth + 1);
            if (!node->a) parser_fail(parser);
        }
    } else if (!strcmp(command, "left")) {
        node = node_create(parser, TINTA_MATH_DELIMITED);
        if (node) {
            node->text = read_delimiter(parser);
            node->a = parse_sequence(parser, depth + 1, "right");
            if (!command_at(parser, "right")) parser_fail(parser);
            else {
                char *right = read_command(parser);
                free(right);
                node->aux = read_delimiter(parser);
            }
            if (!node->text || !node->aux || !node->a) parser_fail(parser);
        }
    } else if (!strcmp(command, "middle")) {
        char *delimiter = read_delimiter(parser);
        if (delimiter) node = make_text_node(
            parser, delimiter, strlen(delimiter));
        free(delimiter);
    } else if (!strcmp(command, "hat") || !strcmp(command, "widehat") ||
               !strcmp(command, "bar") || !strcmp(command, "overline") ||
               !strcmp(command, "underline") || !strcmp(command, "vec") ||
               !strcmp(command, "overrightarrow") ||
               !strcmp(command, "dot") || !strcmp(command, "ddot") ||
               !strcmp(command, "tilde") || !strcmp(command, "widetilde") ||
               !strcmp(command, "overbrace") || !strcmp(command, "underbrace")) {
        const char *accent = "^";
        node = node_create(parser, TINTA_MATH_ACCENT);
        if (!strcmp(command, "bar") || !strcmp(command, "overline")) accent = "_over";
        else if (!strcmp(command, "underline")) accent = "_under";
        else if (!strcmp(command, "vec") ||
                 !strcmp(command, "overrightarrow")) accent = "\xE2\x86\x92";
        else if (!strcmp(command, "dot")) accent = "\xCB\x99";
        else if (!strcmp(command, "ddot")) accent = "\xC2\xA8";
        else if (!strcmp(command, "tilde") || !strcmp(command, "widetilde")) accent = "~";
        else if (!strcmp(command, "overbrace")) accent = "_overbrace";
        else if (!strcmp(command, "underbrace")) accent = "_underbrace";
        if (node) {
            node->text = duplicate_bytes(accent, strlen(accent));
            node->a = parse_argument(parser, depth + 1);
            if (!node->text || !node->a) parser_fail(parser);
        }
    } else if (!strcmp(command, "text") || !strcmp(command, "operatorname")) {
        char *text = read_group_text(parser);
        if (text) {
            node = make_text_node(parser, text, strlen(text));
            if (node) node->style = TINTA_MATH_STYLE_TEXT;
        }
        free(text);
    } else if (!strcmp(command, "mathrm") || !strcmp(command, "textrm") ||
               !strcmp(command, "mathbf") || !strcmp(command, "textbf") ||
               !strcmp(command, "boldsymbol") || !strcmp(command, "bm") ||
               !strcmp(command, "mathit") || !strcmp(command, "mathsf") ||
               !strcmp(command, "mathtt") || !strcmp(command, "mathbb") ||
               !strcmp(command, "mathcal") || !strcmp(command, "mathscr")) {
        node = node_create(parser, TINTA_MATH_STYLE);
        if (node) {
            node->style = (!strcmp(command, "mathbf") ||
                           !strcmp(command, "textbf") ||
                           !strcmp(command, "boldsymbol") ||
                           !strcmp(command, "bm")) ? TINTA_MATH_STYLE_BOLD :
                !strcmp(command, "mathit") ? TINTA_MATH_STYLE_ITALIC :
                !strcmp(command, "mathsf") ? TINTA_MATH_STYLE_SANS :
                !strcmp(command, "mathtt") ? TINTA_MATH_STYLE_MONO :
                !strcmp(command, "mathbb") ? TINTA_MATH_STYLE_BLACKBOARD :
                (!strcmp(command, "mathcal") ||
                 !strcmp(command, "mathscr")) ?
                    TINTA_MATH_STYLE_CALLIGRAPHIC :
                TINTA_MATH_STYLE_ROMAN;
            node->a = parse_group(parser, depth + 1);
            if (!node->a) parser_fail(parser);
        }
    } else if (!strcmp(command, "displaystyle") ||
               !strcmp(command, "textstyle") ||
               !strcmp(command, "scriptstyle") ||
               !strcmp(command, "scriptscriptstyle")) {
        node = make_space_node(parser, 0);
        if (node && !strcmp(command, "displaystyle"))
            node->style = TINTA_MATH_STYLE_DISPLAY;
        else if (node && !strcmp(command, "scriptstyle"))
            node->style = TINTA_MATH_STYLE_SCRIPT;
        else if (node && !strcmp(command, "scriptscriptstyle"))
            node->style = TINTA_MATH_STYLE_SCRIPTSCRIPT;
    } else if (!strcmp(command, "begin")) {
        node = parse_environment(parser, depth + 1);
    } else if (!strcmp(command, "quad")) node = make_space_node(parser, 1.0f);
    else if (!strcmp(command, "qquad")) node = make_space_node(parser, 2.0f);
    else if (!strcmp(command, ",")) node = make_space_node(parser, 0.1667f);
    else if (!strcmp(command, ":")) node = make_space_node(parser, 0.2222f);
    else if (!strcmp(command, ";")) node = make_space_node(parser, 0.2778f);
    else if (!strcmp(command, "!")) node = make_space_node(parser, -0.1667f);
    else if (!strcmp(command, " ")) node = make_space_node(parser, 0.3333f);
    else if (!strcmp(command, "limits") || !strcmp(command, "nolimits")) {
        node = make_space_node(parser, 0);
        if (node) node->limits_mode = !strcmp(command, "limits") ?
            TINTA_MATH_LIMITS_LIMITS : TINTA_MATH_LIMITS_NOLIMITS;
    }
    else if (strlen(command) == 1 && strchr("{}_%#$&", command[0]))
        node = make_text_node(parser, command, 1);
    else parser_fail(parser);
    free(command);
    if (parser->failed) {
        tinta_math_node_destroy(node);
        return NULL;
    }
    return node;
}

static size_t utf8_character_length(unsigned char value) {
    if (value < 0x80) return 1;
    if ((value & 0xE0) == 0xC0) return 2;
    if ((value & 0xF0) == 0xE0) return 3;
    if ((value & 0xF8) == 0xF0) return 4;
    return 0;
}

static bool valid_utf8_character(const char *source, size_t remaining,
                                 size_t length) {
    unsigned char first;
    size_t i;
    if (!source || !length || length > remaining) return false;
    first = (unsigned char)source[0];
    for (i = 1; i < length; i++)
        if (((unsigned char)source[i] & 0xC0) != 0x80) return false;
    if (length == 2 && first < 0xC2) return false;
    if (length == 3) {
        unsigned char second = (unsigned char)source[1];
        if (first == 0xE0 && second < 0xA0) return false;
        if (first == 0xED && second >= 0xA0) return false;
    }
    if (length == 4) {
        unsigned char second = (unsigned char)source[1];
        if (first > 0xF4) return false;
        if (first == 0xF0 && second < 0x90) return false;
        if (first == 0xF4 && second >= 0x90) return false;
    }
    return true;
}

static TintaMathNode *parse_atom(MathParser *parser, size_t depth) {
    TintaMathNode *base;
    TintaMathNode *sub = NULL;
    TintaMathNode *sup = NULL;
    TintaMathLimitsMode limits_mode = TINTA_MATH_LIMITS_AUTO;
    bool suppress_scripts = parser->suppress_scripts;
    size_t length;
    parser->suppress_scripts = false;
    skip_space(parser);
    if (parser->max_depth && depth > parser->max_depth) {
        parser_fail(parser);
        return NULL;
    }
    if (parser->position >= parser->length) return NULL;
    if (parser->source[parser->position] == '{')
        base = parse_group(parser, depth + 1);
    else if (parser->source[parser->position] == '\\')
        base = parse_command(parser, depth + 1);
    else if (parser->source[parser->position] == '~') {
        parser->position++;
        base = make_space_node(parser, 0.3333f);
    } else {
        unsigned char value = (unsigned char)parser->source[parser->position];
        if (strchr("}^_&", value)) {
            parser_fail(parser);
            return NULL;
        }
        length = utf8_character_length(value);
        if (!valid_utf8_character(parser->source + parser->position,
                parser->length - parser->position, length)) {
            parser_fail(parser);
            return NULL;
        }
        base = make_text_node(parser, parser->source + parser->position, length);
        parser->position += length;
    }
    if (!base) return NULL;
    if (suppress_scripts) return base;
    while (!parser->failed && parser->position < parser->length &&
           (command_at(parser, "limits") || command_at(parser, "nolimits"))) {
        char *limits = read_command(parser);
        if (!limits) break;
        limits_mode = !strcmp(limits, "limits") ?
            TINTA_MATH_LIMITS_LIMITS : TINTA_MATH_LIMITS_NOLIMITS;
        free(limits);
        skip_space(parser);
    }
    while (!parser->failed && parser->position < parser->length) {
        char marker = parser->source[parser->position];
        if (marker != '^' && marker != '_' && marker != '\'') break;
        if (marker == '\'') {
            size_t count = 0;
            while (parser->position < parser->length &&
                   parser->source[parser->position] == '\'') {
                parser->position++; count++;
            }
            if (sup) parser_fail(parser);
            else {
                size_t i;
                TintaMathNode *row = node_create(parser, TINTA_MATH_ROW);
                for (i = 0; row && i < count; i++) {
                    TintaMathNode *prime = make_text_node(
                        parser, "\xE2\x80\xB2", 3);
                    if (!prime || !node_add_child(parser, row, prime)) {
                        tinta_math_node_destroy(prime);
                        tinta_math_node_destroy(row);
                        row = NULL;
                    }
                }
                sup = row;
            }
            continue;
        }
        parser->position++;
        if (marker == '^') {
            if (sup) parser_fail(parser);
            else sup = parse_argument(parser, depth + 1);
        } else {
            if (sub) parser_fail(parser);
            else sub = parse_argument(parser, depth + 1);
        }
    }
    if (parser->failed) {
        tinta_math_node_destroy(base);
        tinta_math_node_destroy(sub);
        tinta_math_node_destroy(sup);
        return NULL;
    }
    if (sub || sup) {
        TintaMathNode *script = node_create(parser, TINTA_MATH_SCRIPT);
        if (!script) {
            tinta_math_node_destroy(base);
            tinta_math_node_destroy(sub);
            tinta_math_node_destroy(sup);
            return NULL;
        }
        script->a = base;
        script->b = sub;
        script->c = sup;
        script->limits_mode = limits_mode;
        return script;
    }
    base->limits_mode = limits_mode;
    return base;
}

static TintaMathNode *parse_argument(MathParser *parser, size_t depth) {
    TintaMathNode *result;
    bool previous_suppression;
    skip_space(parser);
    if (parser->position < parser->length &&
        parser->source[parser->position] == '{')
        return parse_group(parser, depth + 1);
    previous_suppression = parser->suppress_scripts;
    parser->suppress_scripts = true;
    result = parse_atom(parser, depth + 1);
    parser->suppress_scripts = previous_suppression;
    return result;
}

static TintaMathNode *parse_sequence(MathParser *parser, size_t depth,
                                     const char *stop) {
    TintaMathNode *row = node_create(parser, TINTA_MATH_ROW);
    if (!row) return NULL;
    while (!parser->failed) {
        TintaMathNode *child;
        skip_space(parser);
        if (parser->position >= parser->length) break;
        if (parser->source[parser->position] == '}' ||
            (stop && !strcmp(stop, "]") &&
             parser->source[parser->position] == ']') ||
            (stop && strcmp(stop, "]") &&
             (parser->source[parser->position] == '&' ||
              (parser->position + 1 < parser->length &&
               parser->source[parser->position] == '\\' &&
               parser->source[parser->position + 1] == '\\') ||
              command_at(parser, "end"))) ||
            (stop && !strcmp(stop, "right") && command_at(parser, "right")))
            break;
        child = parse_atom(parser, depth + 1);
        if (child && child->type == TINTA_MATH_SPACE &&
            child->space_em == 0 &&
            child->limits_mode != TINTA_MATH_LIMITS_AUTO) {
            if (row->child_count) {
                TintaMathNode *previous = row->children[row->child_count - 1];
                previous->limits_mode = child->limits_mode;
            }
            tinta_math_node_destroy(child);
            continue;
        }
        if (!child || !node_add_child(parser, row, child)) {
            tinta_math_node_destroy(child);
            tinta_math_node_destroy(row);
            return NULL;
        }
    }
    if (parser->failed) {
        tinta_math_node_destroy(row);
        return NULL;
    }
    return row;
}

TintaMathParseResult tinta_math_parse(const char *source, size_t length,
                                      size_t max_nodes, size_t max_depth) {
    TintaMathParseResult result;
    MathParser parser;
    memset(&result, 0, sizeof(result));
    memset(&parser, 0, sizeof(parser));
    parser.source = source ? source : "";
    parser.length = length;
    parser.max_nodes = max_nodes;
    parser.max_depth = max_depth;
    result.root = parse_sequence(&parser, 1, NULL);
    skip_space(&parser);
    if (parser.failed || parser.position != parser.length || !result.root) {
        tinta_math_node_destroy(result.root);
        result.root = NULL;
        result.error_offset = parser.failed ? parser.error_offset : parser.position;
        result.node_count = parser.node_count;
        return result;
    }
    result.success = true;
    result.node_count = parser.node_count;
    return result;
}
