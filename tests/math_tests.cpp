#include "test_harness.h"
#include "math_opentype.h"
#include "tinta_math.h"

#include <cstring>
#include <cstdint>

namespace {

TintaMathParseResult parse(const char *source, size_t nodes = 1024,
                           size_t depth = 64) {
    return tinta_math_parse(source, std::strlen(source), nodes, depth);
}

void destroy(TintaMathParseResult &result) {
    tinta_math_node_destroy(result.root);
    result.root = nullptr;
}

void put_u16(uint8_t *data, size_t offset, uint16_t value) {
    data[offset] = static_cast<uint8_t>(value >> 8);
    data[offset + 1] = static_cast<uint8_t>(value);
}

}  // namespace

void run_math_tests(TintaTestContext &tests) {
    auto scripts = parse("x^2+y_{i+1}");
    tests.check(scripts.success && scripts.root && scripts.node_count >= 7,
                "scripts parse into a bounded AST");
    destroy(scripts);

    auto fraction = parse("\\frac{a}{b}+\\sqrt[3]{x}");
    tests.check(fraction.success && fraction.root,
                "fractions and indexed roots parse");
    destroy(fraction);

    auto delimiters = parse("\\left(\\sum_{i=0}^n i\\middle|x\\right)");
    tests.check(delimiters.success && delimiters.root,
                "left, middle, right delimiters parse");
    destroy(delimiters);

    auto matrix = parse(
        "\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}+"
        "\\begin{cases}x&x>0\\\\-x&x\\le0\\end{cases}");
    tests.check(matrix.success && matrix.root,
                "matrix and cases environments parse");
    destroy(matrix);

    auto aligned = parse(
        "\\begin{aligned}a&=b+c\\\\d&=e\\end{aligned}");
    tests.check(aligned.success && aligned.root,
                "aligned environment parses column markers and rows");
    destroy(aligned);

    auto unknown = parse("x+\\notARealCommand{y}");
    tests.check(!unknown.success && !unknown.root,
                "unknown commands reject only the formula");
    destroy(unknown);

    auto too_few_nodes = parse("\\frac{a+b}{c+d}", 3, 64);
    tests.check(!too_few_nodes.success,
                "formula node limit produces a safe failure");
    destroy(too_few_nodes);

    auto too_deep = parse("{{{{{{x}}}}}}", 1024, 3);
    tests.check(!too_deep.success,
                "formula depth limit produces a safe failure");
    destroy(too_deep);

    const char malformed[] = {'x', '+', static_cast<char>(0xE2), '(', 0};
    auto invalid_utf8 = tinta_math_parse(malformed, 4, 1024, 64);
    tests.check(!invalid_utf8.success,
                "malformed UTF-8 produces a safe failure");
    destroy(invalid_utf8);

    uint8_t table[160]{};
    put_u16(table, 0, 1);       // major
    put_u16(table, 2, 0);       // minor
    put_u16(table, 4, 10);      // constants
    put_u16(table, 6, 40);      // glyph info
    put_u16(table, 8, 50);      // variants
    put_u16(table, 22, 0xffec); // axis height = -20
    put_u16(table, 50, 5);      // minimum connector overlap
    put_u16(table, 52, 30);     // vertical coverage -> 80
    put_u16(table, 54, 30);     // horizontal coverage -> 80
    put_u16(table, 56, 1);      // vertical glyph count
    put_u16(table, 58, 0);      // horizontal glyph count
    put_u16(table, 60, 40);     // construction -> 90
    put_u16(table, 80, 1);      // coverage format 1
    put_u16(table, 82, 1);
    put_u16(table, 84, 42);
    put_u16(table, 90, 12);     // assembly -> 102
    put_u16(table, 92, 2);      // two variants
    put_u16(table, 94, 100);
    put_u16(table, 96, 500);
    put_u16(table, 98, 101);
    put_u16(table, 100, 700);
    put_u16(table, 106, 1);     // one assembly part
    put_u16(table, 108, 200);
    put_u16(table, 110, 10);
    put_u16(table, 112, 11);
    put_u16(table, 114, 300);
    put_u16(table, 116, 1);     // extender

    TintaMathOtTable parsed_table{};
    tests.check(tinta_math_ot_parse(table, sizeof(table), &parsed_table) &&
                    parsed_table.axis_height == -20 &&
                    parsed_table.minimum_connector_overlap == 5,
                "OpenType MATH header and big-endian constants parse");
    uint16_t coverage_index = UINT16_MAX;
    tests.check(tinta_math_ot_coverage_index(
                    table, sizeof(table), 80, 42, &coverage_index) &&
                    coverage_index == 0,
                "OpenType coverage format 1 resolves glyphs");
    TintaMathOtVariant variant{};
    tests.check(tinta_math_ot_variant(
                    table, sizeof(table), true, 42, 1, &variant) &&
                    variant.glyph == 101 && variant.advance == 700,
                "OpenType glyph variants are bounds checked");
    TintaMathOtAssemblyPart part{};
    tests.check(tinta_math_ot_assembly_part(
                    table, sizeof(table), true, 42, 0, &part) &&
                    part.glyph == 200 && part.extender &&
                    part.full_advance == 300,
                "OpenType glyph assembly parts are bounds checked");
    tests.check(!tinta_math_ot_parse(table, 12, &parsed_table) &&
                    !tinta_math_ot_variant(
                        table, 96, true, 42, 1, &variant),
                "truncated or corrupt MATH offsets fail safely");

    uint8_t coverage2[24]{};
    put_u16(coverage2, 0, 2);
    put_u16(coverage2, 2, 1);
    put_u16(coverage2, 4, 50);
    put_u16(coverage2, 6, 55);
    put_u16(coverage2, 8, 7);
    tests.check(tinta_math_ot_coverage_index(
                    coverage2, sizeof(coverage2), 0, 53,
                    &coverage_index) && coverage_index == 10,
                "OpenType coverage format 2 resolves range indices");
}
