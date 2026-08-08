#include "mermaid.h"

#include <cstring>
#include <iostream>
#include <string>
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
        auto layout = tinta_mermaid_layout(&result.diagram,
            sizes.data(), sizes.size(), nullptr, 0, 1.0f, 20.0f, 40.0f);
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
        auto layout = tinta_mermaid_layout(&cyclic.diagram,
            sizes.data(), sizes.size(), nullptr, 0, 1.0f, 20.0f, 40.0f);
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

    auto node_limited = tinta_mermaid_parse_limited(
        "flowchart LR\nA --> B\n", std::strlen("flowchart LR\nA --> B\n"),
        1, 10);
    check(!node_limited.success, "Mermaid node limit is enforced during parsing");
    tinta_mermaid_parse_result_destroy(&node_limited);

    auto edge_limited = tinta_mermaid_parse_limited(
        "flowchart LR\nA --> B\nB --> C\n",
        std::strlen("flowchart LR\nA --> B\nB --> C\n"), 10, 1);
    check(!edge_limited.success, "Mermaid edge limit is enforced during parsing");
    tinta_mermaid_parse_result_destroy(&edge_limited);
}

void testSubgraphs() {
    const char *source = R"(flowchart LR
outside --> cluster
subgraph cluster [Outer Group]
  direction TB
  A --> inner
  subgraph inner [Inner Group]
    direction RL
    B --> C
  end
  A --> B
end
cluster --> done
style cluster fill:#112233,stroke:#abcdef,stroke-width:2px
class inner grouped
classDef grouped fill:#223344,color:#ffffff
)";
    auto result = parse(source);
    check(result.success, "nested subgraphs parse");
    if (!result.success)
        std::cerr << "  subgraph parse error line " << result.error_line
                  << ": " << (result.error ? result.error : "unknown") << '\n';
    if (result.success) {
        check(result.diagram.subgraph_count == 2, "nested subgraph count");
        check(result.diagram.node_count == 5, "subgraph member node count");
        check(result.diagram.edge_count == 5, "subgraph endpoint edges resolve");
        const auto *outer = tinta_mermaid_find_subgraph(&result.diagram, "cluster");
        const auto *inner = tinta_mermaid_find_subgraph(&result.diagram, "inner");
        check(outer && std::strcmp(outer->label, "Outer Group") == 0 &&
                  outer->has_direction &&
                  outer->direction == TINTA_MERMAID_TOP_TO_BOTTOM,
              "explicit subgraph title and direction");
        check(inner && inner->parent_subgraph == 0 &&
                  inner->has_direction &&
                  inner->direction == TINTA_MERMAID_RIGHT_TO_LEFT,
              "nested subgraph direction");
        check(outer && outer->style.has_fill &&
                  outer->style.fill.rgb == 0x112233 &&
                  outer->style.has_stroke_width &&
                  outer->style.stroke_width == 2.0f,
              "subgraph direct style");
        check(inner && std::strcmp(inner->class_name, "grouped") == 0,
              "subgraph class assignment");
        check(result.diagram.edges[0].to_subgraph &&
                  result.diagram.edges[0].to == 0 &&
                  result.diagram.edges[4].from_subgraph,
              "forward and trailing subgraph endpoints resolve");

        std::vector<TintaMermaidSize> node_sizes(
            result.diagram.node_count, {100.0f, 52.0f});
        std::vector<TintaMermaidSize> title_sizes(
            result.diagram.subgraph_count, {100.0f, 20.0f});
        auto layout = tinta_mermaid_layout(&result.diagram,
            node_sizes.data(), node_sizes.size(),
            title_sizes.data(), title_sizes.size(),
            1.0f, 32.0f, 78.0f);
        check(layout.node_count == result.diagram.node_count &&
                  layout.subgraph_count == 2,
              "hierarchical subgraph layout completes");
        if (layout.subgraph_count == 2) {
            const auto &outer_box = layout.subgraphs[0];
            const auto &inner_box = layout.subgraphs[1];
            const auto *a = tinta_mermaid_find_node(&result.diagram, "A");
            const auto *b = tinta_mermaid_find_node(&result.diagram, "B");
            const auto *c = tinta_mermaid_find_node(&result.diagram, "C");
            size_t ai = static_cast<size_t>(a - result.diagram.nodes);
            size_t bi = static_cast<size_t>(b - result.diagram.nodes);
            size_t ci = static_cast<size_t>(c - result.diagram.nodes);
            check(inner_box.left >= outer_box.left + 20.0f &&
                      inner_box.right <= outer_box.right - 20.0f &&
                      inner_box.top >= outer_box.top + 36.0f &&
                      inner_box.bottom <= outer_box.bottom - 20.0f,
                  "nested group stays inside parent padding");
            check(layout.nodes[ai].bottom < inner_box.top,
                  "outer TB direction places A above inner group");
            check(layout.nodes[bi].left > layout.nodes[ci].left,
                  "inner RL direction remains independent");
        }
        tinta_mermaid_layout_destroy(&layout);
    }
    tinta_mermaid_parse_result_destroy(&result);

    auto empty = parse("flowchart TB\nsubgraph empty [Nothing here]\nend\n");
    check(empty.success && empty.diagram.node_count == 0 &&
              empty.diagram.subgraph_count == 1,
          "empty subgraph is valid");
    tinta_mermaid_parse_result_destroy(&empty);

    auto titles = parse(
        "flowchart TB\nsubgraph compact[Compact title]\nend\n"
        "subgraph \"Generated title\"\nend\n");
    check(titles.success && titles.diagram.subgraph_count == 2 &&
              std::strcmp(titles.diagram.subgraphs[0].id, "compact") == 0 &&
              std::strcmp(titles.diagram.subgraphs[0].label,
                          "Compact title") == 0 &&
              std::strcmp(titles.diagram.subgraphs[1].label,
                          "Generated title") == 0,
          "compact IDs and quoted implicit titles parse");
    tinta_mermaid_parse_result_destroy(&titles);

    auto membership = parse(
        "flowchart TB\nsubgraph one\nA[First]\nend\n"
        "subgraph two\nA[Second]\nend\n");
    check(!membership.success, "node cannot belong to two subgraphs");
    tinta_mermaid_parse_result_destroy(&membership);

    auto conflict = parse(
        "flowchart TB\ng[Node]\nsubgraph g [Group]\nend\n");
    check(!conflict.success, "node and subgraph IDs cannot conflict");
    tinta_mermaid_parse_result_destroy(&conflict);

    const char *limited_source =
        "flowchart TB\nsubgraph one\nA\nend\n";
    auto limited = tinta_mermaid_parse_limited(
        limited_source, std::strlen(limited_source), 1, 10);
    check(!limited.success, "subgraphs count toward Mermaid node limit");
    tinta_mermaid_parse_result_destroy(&limited);

    std::string deep = "flowchart TB\n";
    for (int i = 0; i < 65; i++)
        deep += "subgraph group" + std::to_string(i) + "\n";
    deep += "A\n";
    for (int i = 0; i < 65; i++) deep += "end\n";
    auto nested_limit = tinta_mermaid_parse(deep.c_str(), deep.size());
    check(!nested_limit.success,
          "Subgraph nesting depth is limited to 64");
    tinta_mermaid_parse_result_destroy(&nested_limit);
}
}

int main() {
    testStyledFlowchart();
    testAliasesChainingAndLayout();
    testSpecialCases();
    testSubgraphs();
    if (failures) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Mermaid C tests passed\n";
    return 0;
}
