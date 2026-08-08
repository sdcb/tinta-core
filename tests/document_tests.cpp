#include "document.h"
#if TINTA_ENABLE_MERMAID
#include "mermaid.h"
#endif

#include <cstring>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

TintaParseResult parse_doc(const char *text, const char *path) {
    return tinta_parse_document(text, std::strlen(text), path, nullptr);
}
}

int main() {
    check(tinta_is_supported_document_path_utf8("notes.md"), ".md supported");
    check(tinta_is_supported_document_path_utf8("notes.MARKDOWN"), ".markdown case insensitive");
    check(tinta_is_supported_document_path_utf16(L"diagram.MMD"), ".mmd supported");
    check(tinta_is_mermaid_document_path_utf8("diagram.mmd"), "Mermaid detected");
    check(!tinta_is_supported_document_path_utf8("notes.txt"), ".txt not listed");
    check(tinta_is_supported_drop_path_utf16(L"notes.txt"), ".txt can drop");

    auto mermaid = parse_doc("flowchart LR\nA --> B\n", "diagram.mmd");
    check(mermaid.success && mermaid.root && mermaid.root->child_count == 1, "Mermaid wrapper");
    if (mermaid.success) {
#if TINTA_ENABLE_MERMAID
        check(mermaid.root->children[0]->type == TINTA_ELEMENT_MERMAID_DIAGRAM,
              "Mermaid element");
#else
        check(mermaid.root->children[0]->type == TINTA_ELEMENT_CODE_BLOCK,
              "disabled Mermaid falls back to source code");
#endif
    }
    tinta_parse_result_destroy(&mermaid);

    auto ext = parse_doc("before ==mark 中文== mid x^2^ and H~2~O ~~gone~~ `==not this==`\n", "notes.md");
    check(ext.success, "extensions parse");
    if (ext.success && ext.root->child_count) {
        int highlights = 0, supers = 0, subs = 0, strikes = 0, code = 0;
        auto *paragraph = ext.root->children[0];
        for (size_t i = 0; i < paragraph->child_count; i++) {
            auto *child = paragraph->children[i];
            if (child->type == TINTA_ELEMENT_HIGHLIGHT) highlights++;
            if (child->type == TINTA_ELEMENT_SUPERSCRIPT) supers++;
            if (child->type == TINTA_ELEMENT_SUBSCRIPT) subs++;
            if (child->type == TINTA_ELEMENT_STRIKETHROUGH) strikes++;
            if (child->type == TINTA_ELEMENT_CODE) {
                code++;
                check(child->child_count && std::strcmp(child->children[0]->text, "==not this==") == 0, "code intact");
            }
        }
        check(highlights == 1 && supers == 1 && subs == 1 && strikes == 1 && code == 1, "extension counts");
    }
    tinta_parse_result_destroy(&ext);

    auto html_scripts = parse_doc(
        "H<sub class='chem'>2</sub>O and x<SUP data-kind='power'>n</SUP>\n",
        "notes.md");
    check(html_scripts.success && html_scripts.root->child_count == 1,
          "inline HTML sub and sup parse");
    if (html_scripts.success && html_scripts.root->child_count == 1) {
        int html_subs = 0;
        int html_supers = 0;
        auto *paragraph = html_scripts.root->children[0];
        for (size_t i = 0; i < paragraph->child_count; i++) {
            html_subs += paragraph->children[i]->type ==
                TINTA_ELEMENT_SUBSCRIPT;
            html_supers += paragraph->children[i]->type ==
                TINTA_ELEMENT_SUPERSCRIPT;
        }
        check(html_subs == 1 && html_supers == 1,
              "inline HTML scripts map to existing AST nodes");
    }
    tinta_parse_result_destroy(&html_scripts);

    const char *details_source =
        "<details open>\n"
        "<summary>Outer <sub>x</sub></summary>\n\n"
        "Paragraph **body**.\n\n"
        "- one\n"
        "- two\n\n"
        "<details>\n"
        "<summary>Inner</summary>\n\n"
        "Nested body.\n\n"
        "</details>\n\n"
        "</details>\n";
    auto details = parse_doc(details_source, "notes.md");
    check(details.success && details.root->child_count == 1 &&
              details.root->children[0]->type == TINTA_ELEMENT_DETAILS,
          "GitHub-style details blocks are regrouped");
    if (details.success && details.root->child_count == 1) {
        auto *outer = details.root->children[0];
        check(outer->open && outer->child_count >= 4 &&
                  outer->children[0]->type == TINTA_ELEMENT_SUMMARY,
              "open details keeps its first summary");
        int nested_details = 0;
        for (size_t i = 1; i < outer->child_count; i++) {
            if (outer->children[i]->type == TINTA_ELEMENT_DETAILS) {
                nested_details++;
                check(!outer->children[i]->open &&
                          outer->children[i]->child_count &&
                          outer->children[i]->children[0]->type ==
                              TINTA_ELEMENT_SUMMARY,
                      "nested details defaults closed");
            }
        }
        check(nested_details == 1,
              "nested details remains inside the outer body");
    }
    tinta_parse_result_destroy(&details);

    auto default_summary = parse_doc(
        "<details>\n\nBody without summary.\n\n</details>\n", "notes.md");
    check(default_summary.success && default_summary.root->child_count == 1 &&
              default_summary.root->children[0]->child_count &&
              default_summary.root->children[0]->children[0]->type ==
                  TINTA_ELEMENT_SUMMARY &&
              default_summary.root->children[0]->children[0]->child_count &&
              std::strcmp(default_summary.root->children[0]->children[0]
                              ->children[0]->text, "Details") == 0,
          "missing summary receives the default label");
    tinta_parse_result_destroy(&default_summary);

    auto multiple_summary = parse_doc(
        "<details open><summary>First</summary><summary>Second</summary><p>Body</p></details>\n",
        "notes.md");
    check(multiple_summary.success && multiple_summary.root->child_count == 1 &&
              multiple_summary.root->children[0]->child_count == 3 &&
              multiple_summary.root->children[0]->children[0]->type ==
                  TINTA_ELEMENT_SUMMARY &&
              multiple_summary.root->children[0]->children[1]->type ==
                  TINTA_ELEMENT_PARAGRAPH,
          "only the first direct summary becomes the disclosure label");
    tinta_parse_result_destroy(&multiple_summary);

    auto open_attribute = parse_doc(
        "<details title='open'><summary>Closed</summary></details>\n",
        "notes.md");
    check(open_attribute.success && open_attribute.root->child_count == 1 &&
              !open_attribute.root->children[0]->open,
          "open is recognized only as an attribute name");
    tinta_parse_result_destroy(&open_attribute);

    auto unclosed_details = parse_doc(
        "<details>\n<summary>Open ended</summary>\n\nBody to EOF.\n",
        "notes.md");
    check(unclosed_details.success &&
              unclosed_details.root->child_count == 1 &&
              unclosed_details.root->children[0]->type ==
                  TINTA_ELEMENT_DETAILS &&
              unclosed_details.root->children[0]->child_count == 2,
          "unclosed details extends to the container end");
    tinta_parse_result_destroy(&unclosed_details);

    auto unmatched_details = parse_doc("</details>\n\nAfter.\n", "notes.md");
    check(unmatched_details.success && unmatched_details.root->child_count == 1 &&
              unmatched_details.root->children[0]->type ==
                  TINTA_ELEMENT_PARAGRAPH,
          "unmatched details end tag is ignored");
    tinta_parse_result_destroy(&unmatched_details);

    auto alert = parse_doc("> [!NOTE]\n> Body text here.\n", "notes.md");
    check(alert.success && alert.root->child_count && alert.root->children[0]->alert_kind == 1, "GitHub alert");
    tinta_parse_result_destroy(&alert);

    auto not_alert = parse_doc("> [!NOTE] trailing words\n", "notes.md");
    check(not_alert.success && not_alert.root->child_count && not_alert.root->children[0]->alert_kind == 0, "invalid alert plain");
    tinta_parse_result_destroy(&not_alert);

    auto br = parse_doc("line one<br>line two<BR/>line three<br />end\n", "notes.md");
    int breaks = 0;
    if (br.success && br.root->child_count)
        for (size_t i = 0; i < br.root->children[0]->child_count; i++)
            if (br.root->children[0]->children[i]->type == TINTA_ELEMENT_HARD_BREAK) breaks++;
    check(breaks == 3, "inline br variants");
    tinta_parse_result_destroy(&br);

    auto blocks = parse_doc("# Heading\n\n- first **bold**\n- second [link](https://example.com)\n\n| A | B |\n|---|---|\n| 1 | 2 |\n", "notes.md");
    check(blocks.success, "block fixture parses");
    if (blocks.success) {
        check(blocks.root->child_count == 3, "heading, list, and table are top-level blocks");
        if (blocks.root->child_count >= 2) {
            auto *list = blocks.root->children[1];
            check(list->type == TINTA_ELEMENT_LIST && list->child_count == 2, "list has two items");
            check(list->tight, "compact Markdown list is marked tight");
            check(list->child_count && list->children[0]->child_count, "list item keeps paragraph content");
            if (list->child_count && list->children[0]->child_count) {
                auto *content = list->children[0]->children[0];
                check(content->type == TINTA_ELEMENT_TEXT || content->type == TINTA_ELEMENT_PARAGRAPH ||
                      content->type == TINTA_ELEMENT_STRONG, "tight-list content is renderable inline content");
            }
        }
        if (blocks.root->child_count >= 3) {
            auto *table = blocks.root->children[2];
            check(table->type == TINTA_ELEMENT_TABLE && table->child_count == 2, "table rows preserved");
        }
    }
    tinta_parse_result_destroy(&blocks);

    auto loose_list = parse_doc("- first paragraph\n\n- second paragraph\n", "notes.md");
    check(loose_list.success && loose_list.root->child_count == 1 &&
          !loose_list.root->children[0]->tight,
          "blank lines produce a loose Markdown list");
    tinta_parse_result_destroy(&loose_list);

    auto tasks = parse_doc("- [ ] pending\n- [x] complete\n", "notes.md");
    check(tasks.success && tasks.root->child_count == 1,
          "task list parses");
    if (tasks.success && tasks.root->child_count == 1) {
        auto *list = tasks.root->children[0];
        check(list->child_count == 2 && list->children[0]->task &&
              !list->children[0]->task_checked && list->children[1]->task &&
              list->children[1]->task_checked,
              "task list state is preserved");
    }
    tinta_parse_result_destroy(&tasks);

    const char *sample = "# Welcome to Tinta\n\n**Tinta** is a viewer.\n\n## Getting Started\n\n- first\n- second\n\n## Features\n\n- one\n- two\n\n```c\nreturn 0;\n```\n";
    auto sample_result = parse_doc(sample, "");
    check(sample_result.success && sample_result.root->child_count == 7, "welcome sample keeps both lists");
    if (sample_result.success && sample_result.root->child_count == 7) {
        check(sample_result.root->children[3]->type == TINTA_ELEMENT_LIST &&
              sample_result.root->children[3]->child_count == 2, "first sample list has items");
        check(sample_result.root->children[5]->type == TINTA_ELEMENT_LIST &&
              sample_result.root->children[5]->child_count == 2, "second sample list has items");
    }
    tinta_parse_result_destroy(&sample_result);

    auto fenced = parse_doc("```mermaid\nflowchart TB\nA --> B\n```\n", "notes.md");
    check(fenced.success && fenced.root->child_count == 1 &&
          fenced.root->children[0]->type == TINTA_ELEMENT_CODE_BLOCK,
          "fenced Mermaid remains a code block AST node");
    if (fenced.success && fenced.root->child_count == 1) {
        check(std::strcmp(fenced.root->children[0]->language, "mermaid") == 0,
              "fenced Mermaid language is preserved");
        auto *code = fenced.root->children[0];
        const char *diagram_source = code->child_count ? code->children[0]->text : code->text;
#if TINTA_ENABLE_MERMAID
        auto diagram = tinta_mermaid_parse(diagram_source, std::strlen(diagram_source));
        check(diagram.success, "fenced Mermaid content parses as a diagram");
        tinta_mermaid_parse_result_destroy(&diagram);
#else
        check(std::strstr(diagram_source, "flowchart TB") != nullptr,
              "disabled Mermaid preserves fenced source");
#endif
    }
    tinta_parse_result_destroy(&fenced);

    auto raw_language = parse_doc("```cpp\nreturn 0;\n```\n", "notes.md");
    check(raw_language.success && raw_language.root->child_count == 1 &&
          std::strcmp(raw_language.root->children[0]->language, "cpp") == 0,
          "code fence language is preserved without display normalization");
    tinta_parse_result_destroy(&raw_language);

    TintaMarkdownOptions limited = tinta_markdown_default_options();
    limited.max_nodes = 1000;
    limited.max_depth = 3;
    const char *deep = "> one\n>> two\n>>> three\n>>>> four\n";
    auto too_deep = tinta_markdown_parse(deep, std::strlen(deep), &limited);
    check(!too_deep.success, "Markdown nesting depth limit is enforced during parsing");
    tinta_parse_result_destroy(&too_deep);

    if (failures) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All document C tests passed\n";
    return 0;
}
