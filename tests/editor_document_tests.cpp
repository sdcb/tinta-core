#include "editor_document.h"
#include "test_harness.h"

#include <random>
#include <string>

namespace {

std::wstring document_text(const TintaEditorDocument &document) {
    std::wstring text(tinta_editor_document_length(&document), L'\0');
    if (!text.empty())
        tinta_editor_document_copy(&document, 0, text.size(), text.data());
    return text;
}

void check_lines(TintaTestContext &tests,
                 const TintaEditorDocument &document,
                 const std::initializer_list<size_t> &lengths,
                 const char *message) {
    bool matches = tinta_editor_document_line_count(&document) == lengths.size();
    size_t index = 0;
    size_t expected_start = 0;
    for (const size_t expected : lengths) {
        size_t start = 0;
        TintaEditorLineMetric metric{};
        if (!tinta_editor_document_get_line(&document, index, &start, &metric) ||
            start != expected_start || metric.length != expected) {
            matches = false;
            break;
        }
        expected_start += expected;
        index++;
    }
    tests.check(matches, message);
}

} // namespace

void run_editor_document_tests(TintaTestContext &tests) {
    TintaEditorDocument document{};
    tinta_editor_document_init(&document, 20.0f);

    tests.check(tinta_editor_document_assign(
                    &document, L"one\r\ntwo\rthree\n", 15, true, 20.0f),
                "editor document assignment");
    tests.check(document_text(document) == L"one\ntwo\nthree\n",
                "editor document LF normalization");
    check_lines(tests, document, {4, 4, 6, 0},
                "editor document initial line index");

    TintaEditorReplaceResult result{};
    tests.check(tinta_editor_document_replace(
                    &document, 4, 8, L"TWO\n2", 5, true,
                    4, 8, 9, 9, 20.0f, true, &result),
                "editor document replacement");
    tests.check(document_text(document) == L"one\nTWO\n2three\n",
                "editor document replacement value");
    check_lines(tests, document, {4, 4, 7, 0},
                "editor document replacement line index");
    tests.check(tinta_editor_document_can_undo(&document),
                "editor document undo available");

    size_t anchor = 0;
    size_t caret = 0;
    tests.check(tinta_editor_document_undo(
                    &document, &anchor, &caret, 20.0f),
                "editor document undo");
    tests.check(document_text(document) == L"one\ntwo\nthree\n" &&
                    anchor == 4 && caret == 8,
                "editor document undo value and selection");
    tests.check(tinta_editor_document_redo(
                    &document, &anchor, &caret, 20.0f),
                "editor document redo");
    tests.check(document_text(document) == L"one\nTWO\n2three\n" &&
                    anchor == 9 && caret == 9,
                "editor document redo value and selection");

    std::wstring large;
    large.reserve(TINTA_EDITOR_ROPE_CHUNK * 3);
    for (size_t line = 0; line < 20000; line++) {
        large += L"line";
        large += std::to_wstring(line);
        large += L'\n';
    }
    tests.check(tinta_editor_document_assign(
                    &document, large.data(), large.size(), true, 18.0f),
                "editor document multi-page assignment");
    const size_t middle = large.size() / 2;
    tests.check(tinta_editor_document_replace(
                    &document, middle, middle, L"inserted\n", 9, true,
                    middle, middle, middle + 9, middle + 9,
                    18.0f, true, nullptr),
                "editor document multi-page insertion");
    tests.check(tinta_editor_document_length(&document) == large.size() + 9 &&
                    tinta_editor_document_line_count(&document) == 20002,
                "editor document aggregate update");

    const wchar_t invalid[] = {L'a', L'\0', L'b'};
    tests.check(!tinta_editor_document_assign(
                    &document, invalid, 3, true, 18.0f),
                "editor document explicit NUL rejection");

    tests.check(tinta_editor_document_assign(
                    &document, L"", 0, true, 18.0f),
                "editor coalescing reset");
    for (size_t index = 0; index < 3; index++) {
        const wchar_t character = L'a' + static_cast<wchar_t>(index);
        tests.check(tinta_editor_document_replace(
                        &document, index, index, &character, 1, true,
                        index, index, index + 1, index + 1,
                        18.0f, true, nullptr),
                    "editor typing transaction");
        if (index)
            tinta_editor_document_coalesce_last(
                &document, TINTA_EDITOR_UNDO_COALESCE_TYPING);
    }
    tests.check(document.undo.len == 1 &&
                    tinta_editor_document_undo(
                        &document, &anchor, &caret, 18.0f) &&
                    document_text(document).empty(),
                "editor typing undo coalescing");

    std::mt19937_64 random(0x54494e5441ull);
    std::wstring model;
    tests.check(tinta_editor_document_assign(
                    &document, nullptr, 0, true, 18.0f),
                "editor randomized reset");
    bool randomized_ok = true;
    for (size_t step = 0; step < 2000 && randomized_ok; step++) {
        size_t start = model.empty() ? 0 :
            static_cast<size_t>(random() % (model.size() + 1));
        size_t end = start;
        if (!model.empty())
            end += static_cast<size_t>(random() %
                (std::min<size_t>(8, model.size() - start) + 1));
        std::wstring inserted;
        const size_t insert_length = static_cast<size_t>(random() % 7);
        for (size_t index = 0; index < insert_length; index++) {
            const uint64_t choice = random() % 14;
            inserted += choice == 0 ? L'\n' :
                        choice == 1 ? L'\r' :
                        static_cast<wchar_t>(L'a' + choice % 10);
        }
        std::wstring normalized;
        for (size_t index = 0; index < inserted.size(); index++) {
            if (inserted[index] == L'\r' && index + 1 < inserted.size() &&
                inserted[index + 1] == L'\n') index++;
            normalized += inserted[index] == L'\r' ? L'\n' : inserted[index];
        }
        if (!tinta_editor_document_replace(
                &document, start, end, inserted.data(), inserted.size(), true,
                start, end, start + normalized.size(),
                start + normalized.size(), 18.0f, false, nullptr)) {
            randomized_ok = false;
            break;
        }
        model.replace(start, end - start, normalized);
        randomized_ok = document_text(document) == model;
        size_t expected_lines = 1;
        for (wchar_t character : model)
            if (character == L'\n') expected_lines++;
        randomized_ok = randomized_ok &&
            tinta_editor_document_line_count(&document) == expected_lines;
    }
    tests.check(randomized_ok, "editor randomized model differential");

    tinta_editor_document_destroy(&document);

#ifdef TINTA_EDITOR_TEST_ALLOCATOR
    const std::wstring oom_text =
        L"first line\nsecond line with emoji \xd83d\xde00\nthird line\n";
    size_t assign_allocations = 0;
    tinta_editor_test_allocator_reset();
    {
        TintaEditorDocument measured{};
        tinta_editor_document_init(&measured, 18.0f);
        tinta_editor_test_allocator_reset();
        tests.check(tinta_editor_document_assign(
                        &measured, oom_text.data(), oom_text.size(), true,
                        18.0f),
                    "editor OOM assignment measurement");
        assign_allocations = tinta_editor_test_allocator_calls();
        tinta_editor_document_destroy(&measured);
    }
    bool assign_oom_ok = assign_allocations != 0 &&
                         tinta_editor_test_allocator_live() == 0;
    for (size_t failure = 1;
         failure <= assign_allocations && assign_oom_ok; failure++) {
        TintaEditorDocument failed{};
        tinta_editor_test_allocator_reset();
        tinta_editor_document_init(&failed, 18.0f);
        tinta_editor_test_allocator_fail_on(failure);
        assign_oom_ok = !tinta_editor_document_assign(
            &failed, oom_text.data(), oom_text.size(), true, 18.0f);
        tinta_editor_test_allocator_reset();
        assign_oom_ok = assign_oom_ok &&
            tinta_editor_document_assign(
                &failed, oom_text.data(), oom_text.size(), true, 18.0f);
        tinta_editor_document_destroy(&failed);
        assign_oom_ok = assign_oom_ok &&
            tinta_editor_test_allocator_live() == 0;
    }
    tests.check(assign_oom_ok, "editor assignment exact OOM sweep");

    size_t replace_allocations = 0;
    {
        TintaEditorDocument measured{};
        tinta_editor_test_allocator_reset();
        tinta_editor_document_init(&measured, 18.0f);
        tinta_editor_document_assign(
            &measured, oom_text.data(), oom_text.size(), true, 18.0f);
        tinta_editor_test_allocator_reset();
        tests.check(tinta_editor_document_replace(
                        &measured, 6, 17, L"replacement\nlines", 17, true,
                        6, 17, 23, 23, 18.0f, true, nullptr),
                    "editor OOM replace measurement");
        replace_allocations = tinta_editor_test_allocator_calls();
        tinta_editor_document_destroy(&measured);
    }
    bool replace_oom_ok = replace_allocations != 0 &&
                          tinta_editor_test_allocator_live() == 0;
    for (size_t failure = 1;
         failure <= replace_allocations && replace_oom_ok; failure++) {
        TintaEditorDocument failed{};
        tinta_editor_test_allocator_reset();
        tinta_editor_document_init(&failed, 18.0f);
        tinta_editor_document_assign(
            &failed, oom_text.data(), oom_text.size(), true, 18.0f);
        const std::wstring before = document_text(failed);
        tinta_editor_test_allocator_fail_on(failure);
        replace_oom_ok = !tinta_editor_document_replace(
            &failed, 6, 17, L"replacement\nlines", 17, true,
            6, 17, 23, 23, 18.0f, true, nullptr);
        replace_oom_ok = replace_oom_ok && document_text(failed) == before;
        tinta_editor_test_allocator_reset();
        replace_oom_ok = replace_oom_ok &&
            tinta_editor_document_replace(
                &failed, 6, 17, L"replacement\nlines", 17, true,
                6, 17, 23, 23, 18.0f, true, nullptr);
        tinta_editor_document_destroy(&failed);
        replace_oom_ok = replace_oom_ok &&
            tinta_editor_test_allocator_live() == 0;
    }
    tests.check(replace_oom_ok, "editor replace exact OOM sweep");
    tinta_editor_test_allocator_reset();
#endif
}
