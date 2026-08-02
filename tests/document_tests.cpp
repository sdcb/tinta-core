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

    if (failures) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All document C tests passed\n";
    return 0;
}
