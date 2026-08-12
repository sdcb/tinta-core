#include "test_harness.h"

void test_core_vector(TintaTestContext &tests);
void test_core_string8(TintaTestContext &tests);
void test_core_string16(TintaTestContext &tests);
void test_core_newlines(TintaTestContext &tests);
void test_core_map(TintaTestContext &tests);
void run_document_tests(TintaTestContext &tests);
void run_editor_document_tests(TintaTestContext &tests);
void run_md4c_tests(TintaTestContext &tests);
#if TINTA_ENABLE_MATH
void run_math_tests(TintaTestContext &tests);
#endif
#if TINTA_ENABLE_SVG
void run_svg_tests(TintaTestContext &tests);
#endif
#if TINTA_ENABLE_MERMAID
void run_mermaid_tests(TintaTestContext &tests);
#endif
#if TINTA_ENABLE_SYNTAX
void test_syntax_languages(TintaTestContext &tests);
void test_syntax_token_classes(TintaTestContext &tests);
void test_syntax_type_and_function(TintaTestContext &tests);
#endif

int main() {
    static const TintaTestCase cases[] = {
        {"core.vector", test_core_vector},
        {"core.string8", test_core_string8},
        {"core.string16", test_core_string16},
        {"core.newlines", test_core_newlines},
        {"core.map", test_core_map},
        {"document", run_document_tests},
        {"editor.document", run_editor_document_tests},
        {"md4c", run_md4c_tests},
#if TINTA_ENABLE_MATH
        {"math", run_math_tests},
#endif
#if TINTA_ENABLE_SVG
        {"svg", run_svg_tests},
#endif
#if TINTA_ENABLE_MERMAID
        {"mermaid", run_mermaid_tests},
#endif
#if TINTA_ENABLE_SYNTAX
        {"syntax.languages", test_syntax_languages},
        {"syntax.tokens", test_syntax_token_classes},
        {"syntax.types", test_syntax_type_and_function},
#endif
    };
    return tinta_run_tests("unit", cases, sizeof(cases) / sizeof(cases[0]));
}
