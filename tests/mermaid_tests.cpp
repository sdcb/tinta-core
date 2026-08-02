#include "mermaid.h"

#include <cstring>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

TintaMermaidParseResult parse(const char *source) {
    return tinta_mermaid_parse(source, std::strlen(source));
}

void testStyledFlowchart() {
    const char *source = R"(flowchart TB
classDef start fill:#fff7ed,stroke:#f97316,color:#7c2d12,stroke-width:2px;
Start["Begin<br/>enrollment"]:::start --> Choice{"Assigned?"}
Choice -->|Yes| Done["Complete"]
Choice -->|No| Retry(["Retry"])
)";
    auto result = parse(source);
    check(result.success, "styled flowchart parses");
    if (result.success) {
        check(result.diagram.direction == TINTA_MERMAID_TOP_TO_BOTTOM, "TB direction");
        check(result.diagram.node_count == 4, "all nodes parsed");
        check(result.diagram.edge_count == 3, "all edges parsed");
        const auto *start = tinta_mermaid_find_node(&result.diagram, "Start");
        const auto *choice = tinta_mermaid_find_node(&result.diagram, "Choice");
        const auto *retry = tinta_mermaid_find_node(&result.diagram, "Retry");
        check(start && std::strcmp(start->label, "Begin\nenrollment") == 0, "HTML breaks");
        check(start && std::strcmp(start->class_name, "start") == 0, "node class");
        check(choice && choice->shape == TINTA_MERMAID_DIAMOND, "diamond shape");
        check(retry && retry->shape == TINTA_MERMAID_STADIUM, "stadium shape");
        const auto *style = tinta_mermaid_find_class_style(&result.diagram, "start");
        check(style && style->has_fill && style->fill.rgb == 0xFFF7ED, "class fill");
        check(style && style->has_stroke_width && style->stroke_width == 2.0f, "stroke width");
        check(std::strcmp(result.diagram.edges[1].label, "Yes") == 0, "edge label");
    }
    tinta_mermaid_parse_result_destroy(&result);
}

void testAliasesChainingAndLayout() {
    auto result = parse("graph LR\nA --> B --> C\nclass A,B emphasized\nstyle C fill:#abc,stroke:#123456\n");
    check(result.success, "graph alias parses");
    if (result.success) {
        check(result.diagram.node_count == 3, "chained nodes");
        check(result.diagram.edge_count == 2, "chained edges");
        const auto *c = tinta_mermaid_find_node(&result.diagram, "C");
        check(c && c->style.has_fill && c->style.fill.rgb == 0xAABBCC, "short color");
        std::vector<TintaMermaidSize> sizes(result.diagram.node_count, {100.0f, 50.0f});
        auto layout = tinta_mermaid_layout(&result.diagram, sizes.data(), sizes.size(), 20.0f, 40.0f);
        check(layout.node_count == 3, "layout node count");
        check(layout.nodes[0].right < layout.nodes[1].left, "LR target right of source");
        tinta_mermaid_layout_destroy(&layout);
    }
    tinta_mermaid_parse_result_destroy(&result);
}

void testSpecialCases() {
    auto unsupported = parse("sequenceDiagram\nAlice->>Bob: Hello\n");
    check(!unsupported.success && unsupported.error_line == 1, "unsupported diagram rejected");
    tinta_mermaid_parse_result_destroy(&unsupported);

    auto semicolon = parse("\xEF\xBB\xBF" "flowchart LR; A[\"One; still one label\"] -->|Yes; still yes| B; B --> C;");
    check(semicolon.success, "BOM and semicolon statements");
    if (semicolon.success) {
        check(semicolon.diagram.node_count == 3, "semicolon nodes");
        check(std::strcmp(semicolon.diagram.edges[0].label, "Yes; still yes") == 0, "semicolon label");
    }
    tinta_mermaid_parse_result_destroy(&semicolon);

    auto cyclic = parse("flowchart TB\nA --> B\nB --> A\nA --> A\n");
    check(cyclic.success, "cyclic parses");
    if (cyclic.success) {
        std::vector<TintaMermaidSize> sizes(cyclic.diagram.node_count, {100.0f, 50.0f});
        auto layout = tinta_mermaid_layout(&cyclic.diagram, sizes.data(), sizes.size(), 20.0f, 40.0f);
        check(layout.node_count == 2, "cyclic layout nodes");
        check(layout.nodes[0].top == 0.0f && layout.nodes[0].bottom < layout.nodes[1].top,
              "cyclic ranks do not overlap");
        tinta_mermaid_layout_destroy(&layout);
    }
    tinta_mermaid_parse_result_destroy(&cyclic);

    auto newline = parse("graph TD\nBG[background thread\\nengine.stop] --> TK[main]\n");
    const auto *bg = newline.success ? tinta_mermaid_find_node(&newline.diagram, "BG") : nullptr;
    check(bg && std::strcmp(bg->label, "background thread\nengine.stop") == 0, "literal newline");
    tinta_mermaid_parse_result_destroy(&newline);

    auto attributes = parse("flowchart TD\nA@{ shape: rounded, label: \"Fancy\" } --> B[Plain]\n");
    check(!attributes.success, "v11 attributes rejected");
    tinta_mermaid_parse_result_destroy(&attributes);
}
}

int main() {
    testStyledFlowchart();
    testAliasesChainingAndLayout();
    testSpecialCases();
    if (failures) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Mermaid C tests passed\n";
    return 0;
}
