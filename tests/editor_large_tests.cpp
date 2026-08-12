#include "editor_document.h"

#include <iostream>
#include <string>

int main() {
    constexpr size_t target_units = 50u * 1024u * 1024u;
    constexpr size_t target_lines = 1000000u;
    std::wstring text;
    text.reserve(target_units);
    for (size_t line = 0; line < target_lines; line++) {
        text += L"01234567890123456789012345678901234567890123456789\n";
    }
    while (text.size() < target_units) text += L'x';

    TintaEditorDocument document{};
    tinta_editor_document_init(&document, 18.0f);
    bool ok = tinta_editor_document_assign(
        &document, text.data(), text.size(), true, 18.0f);
    ok = ok && tinta_editor_document_length(&document) == text.size() &&
         tinta_editor_document_line_count(&document) == target_lines + 1;
    const size_t middle = text.size() / 2;
    ok = ok && tinta_editor_document_replace(
        &document, 0, 0, L"start\n", 6, true,
        0, 0, 6, 6, 18.0f, true, nullptr);
    ok = ok && tinta_editor_document_replace(
        &document, middle, middle + 16, L"middle\n", 7, true,
        middle, middle + 16, middle + 7, middle + 7,
        18.0f, true, nullptr);
    const size_t end = tinta_editor_document_length(&document);
    ok = ok && tinta_editor_document_replace(
        &document, end, end, L"\nend", 4, true,
        end, end, end + 4, end + 4, 18.0f, true, nullptr);
    ok = ok && tinta_editor_document_undo(
        &document, nullptr, nullptr, 18.0f);
    ok = ok && tinta_editor_document_line_from_position(
        &document, tinta_editor_document_length(&document) / 2) <
        tinta_editor_document_line_count(&document);
    tinta_editor_document_destroy(&document);
#ifdef TINTA_EDITOR_TEST_ALLOCATOR
    ok = ok && tinta_editor_test_allocator_live() == 0;
#endif
    if (!ok) {
        std::cerr << "100 MiB editor stress test failed\n";
        return 1;
    }
    return 0;
}
