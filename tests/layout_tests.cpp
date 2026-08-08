#include <windows.h>

#include "app.h"

#include <cmath>
#include <cstring>
#include <cwchar>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

TintaTextRun *find_run(TintaApp &app, const wchar_t *text, size_t occurrence = 0) {
    for (size_t i = 0; i < app.text_runs.len; i++) {
        auto *run = TINTA_VEC_PTR(TintaTextRun, app.text_runs, i);
        if (run->text && std::wcscmp(run->text, text) == 0) {
            if (!occurrence) return run;
            occurrence--;
        }
    }
    return nullptr;
}

TintaTextRun *find_run_containing(TintaApp &app, const wchar_t *text) {
    for (size_t i = 0; i < app.text_runs.len; i++) {
        auto *run = TINTA_VEC_PTR(TintaTextRun, app.text_runs, i);
        if (run->text && std::wcsstr(run->text, text)) return run;
    }
    return nullptr;
}

#if TINTA_ENABLE_MERMAID
TintaDrawRect *find_outline(TintaApp &app, TintaDrawShape shape) {
    for (size_t i = 0; i < app.rects.len; i++) {
        auto *rect = TINTA_VEC_PTR(TintaDrawRect, app.rects, i);
        if (rect->outline && rect->shape == shape) return rect;
    }
    return nullptr;
}
#endif

bool load(TintaApp &app, const char *markdown) {
    return tinta_app_load_source(
               &app, markdown, std::strlen(markdown), L"layout-tests.md") &&
           tinta_layout_document(&app);
}

void test_inline_styles(TintaApp &app) {
    const char *source =
        "**bold `code`, tail**\n\n"
        "*italic `code`*\n\n"
        "***both `code`***\n\n"
        "[link `code`](https://example.com)\n\n"
        "**x^2^** and **bold ~~gone~~ ==mark==**\n";
    check(load(app, source), "nested inline style document lays out");
    if (!app.layout_complete) return;

    auto *bold_code = find_run(app, L"code", 0);
    auto *italic_code = find_run(app, L"code", 1);
    auto *both_code = find_run(app, L"code", 2);
    auto *both_text = find_run(app, L"both ");
    auto *link_code = find_run(app, L"code", 3);
    auto *punctuation = find_run(app, L", tail");
    auto *superscript = find_run(app, L"2");
    auto *gone = find_run(app, L"gone");
    auto *mark = find_run(app, L"mark");

    check(bold_code && bold_code->layout &&
              bold_code->layout->lpVtbl->GetFontWeight(bold_code->layout) ==
                  TINTA_DWRITE_FONT_WEIGHT_BOLD,
          "inline code inherits bold");
    check(italic_code && italic_code->layout &&
              italic_code->layout->lpVtbl->GetFontStyle(italic_code->layout) ==
                  TINTA_DWRITE_FONT_STYLE_ITALIC,
          "inline code inherits italic");
    check(both_code && both_code->layout &&
              both_code->layout->lpVtbl->GetFontWeight(both_code->layout) ==
                  TINTA_DWRITE_FONT_WEIGHT_BOLD &&
              both_code->layout->lpVtbl->GetFontStyle(both_code->layout) ==
                  TINTA_DWRITE_FONT_STYLE_ITALIC,
          "inline code combines bold and italic");
    check(both_text && both_text->layout &&
              both_text->layout->lpVtbl->GetFontWeight(both_text->layout) ==
                  TINTA_DWRITE_FONT_WEIGHT_BOLD &&
              both_text->layout->lpVtbl->GetFontStyle(both_text->layout) ==
                  TINTA_DWRITE_FONT_STYLE_ITALIC,
          "ordinary inline text combines bold and italic");
    check(link_code && link_code->url &&
              std::strcmp(link_code->url, "https://example.com") == 0 &&
              link_code->color == app.theme->link && link_code->underline,
          "inline code preserves link behavior and color");
    check(bold_code && punctuation &&
              std::fabs(punctuation->x - bold_code->x - bold_code->width - 4.0f) < 1.5f,
          "inline code advances by pill padding instead of a full space");
    check(superscript && superscript->layout &&
              std::fabs(superscript->layout->lpVtbl->GetFontSize(
                            superscript->layout) -
                        app.small_format->lpVtbl->GetFontSize(app.small_format)) < 0.01f,
          "superscript nested in emphasis keeps the small format");
    check(gone && gone->strikethrough && gone->layout &&
              gone->layout->lpVtbl->GetFontWeight(gone->layout) ==
                  TINTA_DWRITE_FONT_WEIGHT_BOLD,
          "nested strikethrough keeps the inherited emphasis");
    check(mark && mark->layout &&
              mark->layout->lpVtbl->GetFontWeight(mark->layout) ==
                  TINTA_DWRITE_FONT_WEIGHT_BOLD,
          "nested highlight keeps the inherited emphasis");
}

void test_block_collapse(TintaApp &app) {
    const char *source =
        "Before\n\n"
        "```cpp\n"
        "const char *hidden_body = \"wide wide wide wide wide wide\";\n"
        "```\n\n"
        "After\n";
    check(load(app, source), "collapsible code document lays out");
    if (!app.layout_complete || app.code_blocks.len != 1) return;

    auto *block = TINTA_VEC_PTR(TintaCodeBlock, app.code_blocks, 0);
    auto *body = find_run_containing(app, L"hidden_body");
    const float expanded_height = app.content_height;
    const size_t document_length = app.doc_text.len;
    const int header_x = block->rect.left + 40;
    const int header_y = block->rect.top + 16;
    check(block->expansion > 0.999f,
          "code blocks are expanded by default");
    check(body && tinta_run_is_visually_exposed(&app, body),
          "expanded code body is visually exposed");

    if (app.horizontal_regions.len == 1) {
        auto *region = TINTA_VEC_PTR(
            TintaHorizontalRegion, app.horizontal_regions, 0);
        region->scroll_x = 17.0f;
    }
    check(tinta_toggle_collapsible_at(&app, header_x, header_y, false) &&
              tinta_layout_document(&app),
          "code header immediately collapses its block");
    block = TINTA_VEC_PTR(TintaCodeBlock, app.code_blocks, 0);
    body = find_run_containing(app, L"hidden_body");
    check(block->rect.bottom - block->rect.top == 32,
          "collapsed block keeps only its 32 DIP header");
    check(app.content_height < expanded_height,
          "collapsed block moves following content upward");
    check(app.doc_text.len == document_length && body,
          "collapsed content remains in the logical document");
    check(body && !tinta_run_is_visually_exposed(&app, body),
          "collapsed body has no visible boundary");
    if (app.horizontal_regions.len == 1) {
        auto *region = TINTA_VEC_PTR(
            TintaHorizontalRegion, app.horizontal_regions, 0);
        check(std::fabs(region->scroll_x - 17.0f) < 0.01f,
              "collapse preserves block-local horizontal scroll");
        check(region->expansion < 0.001f,
              "collapsed region hides its body and scrollbar");
    }

    check(tinta_toggle_collapsible_at(&app, header_x, header_y, true),
          "collapsed block starts an expansion animation");
    const ULONGLONG expand_start = app.block_animation_tick;
    check(tinta_block_animation_tick(&app, expand_start + 90) &&
              tinta_layout_document(&app),
          "block animation advances at its midpoint");
    block = TINTA_VEC_PTR(TintaCodeBlock, app.code_blocks, 0);
    check(block->expansion > 0.05f && block->expansion < 0.95f,
          "mid-animation block has a partial visible height");

    check(tinta_toggle_collapsible_at(
              &app, block->rect.left + 40, block->rect.top + 16, true),
          "block animation reverses from its current progress");
    const ULONGLONG reverse_start = app.block_animation_tick;
    check(tinta_block_animation_tick(&app, reverse_start + 180) &&
              tinta_layout_document(&app),
          "reversed block animation completes smoothly");
    block = TINTA_VEC_PTR(TintaCodeBlock, app.code_blocks, 0);
    check(block->expansion < 0.001f &&
              !tinta_block_animation_active(&app),
          "reversed animation ends collapsed");

    body = find_run_containing(app, L"hidden_body");
    check(body && tinta_expand_run_block(&app, body) &&
              tinta_layout_document(&app),
          "logical access immediately expands a hidden run");
    block = TINTA_VEC_PTR(TintaCodeBlock, app.code_blocks, 0);
    check(block->expansion > 0.999f,
          "immediate logical expansion restores the full body");

    check(load(app, "```cpp\nreplacement\n```\n"),
          "replacement document lays out after collapse testing");
    check(app.block_collapse_states.len == 1 &&
              TINTA_VEC_AT(TintaBlockCollapseState,
                           app.block_collapse_states, 0).progress > 0.999f,
          "new documents reset collapse state to expanded");
}

#if TINTA_ENABLE_MERMAID
void test_mermaid_layout(TintaApp &app) {
    const char *source =
        "```mermaid\n"
        "flowchart LR\n"
        "A[Rect] --> B{Feature}\n"
        "B --> C{{Hex}}\n"
        "C --> D((Circle))\n"
        "```\n";
    check(load(app, source), "Mermaid geometry document lays out");
    if (!app.layout_complete) return;

    auto *rectangle = find_outline(app, TINTA_DRAW_SHAPE_RECTANGLE);
    auto *diamond = find_outline(app, TINTA_DRAW_SHAPE_DIAMOND);
    auto *hexagon = find_outline(app, TINTA_DRAW_SHAPE_HEXAGON);
    auto *circle = find_outline(app, TINTA_DRAW_SHAPE_ELLIPSE);
    auto *feature = find_run(app, L"Feature");

    check(rectangle && rectangle->rect.right - rectangle->rect.left >= 119 &&
              rectangle->rect.bottom - rectangle->rect.top >= 51,
          "rectangle uses the upstream minimum size");
    check(diamond && diamond->rect.right - diamond->rect.left >= 149 &&
              diamond->rect.bottom - diamond->rect.top >= 81,
          "diamond uses shape-aware dimensions");
    check(hexagon && hexagon->rect.right - hexagon->rect.left >= 159,
          "hexagon reserves additional horizontal space");
    check(circle && std::abs((circle->rect.right - circle->rect.left) -
                             (circle->rect.bottom - circle->rect.top)) <= 1,
          "circle remains square");

    if (feature && diamond) {
        FLOAT first_x = 0;
        FLOAT first_y = 0;
        FLOAT last_x = 0;
        FLOAT last_y = 0;
        TintaDWriteHitTestMetrics metrics{};
        bool hit_ok = SUCCEEDED(feature->layout->lpVtbl->HitTestTextPosition(
                          feature->layout, 0, FALSE,
                          &first_x, &first_y, &metrics)) &&
                      SUCCEEDED(feature->layout->lpVtbl->HitTestTextPosition(
                          feature->layout,
                          static_cast<UINT32>(feature->text_length - 1), TRUE,
                          &last_x, &last_y, &metrics));
        float text_center = feature->x + (first_x + last_x) * 0.5f;
        float shape_center =
            (diamond->rect.left + diamond->rect.right) * 0.5f;
        check(hit_ok && std::fabs(text_center - shape_center) < 2.0f,
              "Mermaid label is horizontally centered in the diamond");
        check(std::fabs((feature->y + feature->height * 0.5f) -
                        (diamond->rect.top + diamond->rect.bottom) * 0.5f) < 2.0f,
              "Mermaid label is vertically centered in the diamond");
    } else {
        check(false, "Mermaid Feature label and diamond are present");
    }

    check(app.horizontal_regions.len == 1,
          "Mermaid creates one horizontal scrolling region");
    if (feature && app.horizontal_regions.len == 1) {
        auto *region = TINTA_VEC_PTR(
            TintaHorizontalRegion, app.horizontal_regions, 0);
        LONG first_outline_top = LONG_MAX;
        for (size_t i = 0; i < app.rects.len; i++) {
            auto *rect = TINTA_VEC_PTR(TintaDrawRect, app.rects, i);
            if (rect->outline && rect->horizontal_region == 0 &&
                rect->rect.top < first_outline_top)
                first_outline_top = rect->rect.top;
        }
        check(first_outline_top >= region->viewport.top + 1,
              "Mermaid diagram leaves one physical pixel below its header");
        check(region->overflow, "wide Mermaid overflows inside its block");
        float before = region->scroll_x;
        check(tinta_horizontal_region_scroll_at(
                  &app,
                  (region->viewport.left + region->viewport.right) / 2,
                  (region->viewport.top + region->viewport.bottom) / 2,
                  120.0f) && region->scroll_x > before,
              "Mermaid region scrolls independently");
        FLOAT glyph_x = 0;
        FLOAT glyph_y = 0;
        TintaDWriteHitTestMetrics metrics{};
        if (SUCCEEDED(feature->layout->lpVtbl->HitTestTextPosition(
                feature->layout, 0, FALSE, &glyph_x, &glyph_y, &metrics))) {
            float offset = static_cast<float>(region->viewport.left) -
                           region->content_left - region->scroll_x;
            size_t position = SIZE_MAX;
            tinta_hit_test(&app, feature->x + glyph_x + offset + 1.0f,
                           feature->y + glyph_y + 1.0f,
                           &position, nullptr);
            check(position >= feature->doc_start &&
                      position <= feature->doc_start + feature->doc_length,
                  "Mermaid hit testing follows block-local scrolling");
        }
    }
    if (app.mermaid_blocks.len == 1) {
        auto *block = TINTA_VEC_PTR(TintaMermaidBlock, app.mermaid_blocks, 0);
        const int header_x = block->rect.left + 40;
        const int header_y = block->rect.top + 16;
        check(tinta_toggle_collapsible_at(&app, header_x, header_y, false) &&
                  tinta_layout_document(&app),
              "Mermaid header collapses its rendered diagram");
        block = TINTA_VEC_PTR(TintaMermaidBlock, app.mermaid_blocks, 0);
        feature = find_run(app, L"Feature");
        check(block->rect.bottom - block->rect.top == 32 && feature &&
                  !tinta_run_is_visually_exposed(&app, feature),
              "collapsed Mermaid keeps only its header and hides labels");
        check(feature && tinta_expand_run_block(&app, feature) &&
                  tinta_layout_document(&app),
              "Mermaid hidden content can expand immediately");
        block = TINTA_VEC_PTR(TintaMermaidBlock, app.mermaid_blocks, 0);
        check(block->expansion > 0.999f,
              "expanded Mermaid restores its diagram");
    }
}
#endif

}  // namespace

int main() {
    if (!tinta_shared_graphics_initialize()) {
        std::cerr << "shared graphics initialization failed\n";
        return 1;
    }

    TintaSettings settings{};
    settings.theme_index = 0;
    settings.zoom = 1.0f;
    settings.width = 520;
    settings.height = 700;

    TintaApp app{};
    if (!tinta_app_init(&app, GetModuleHandleW(nullptr), &settings)) {
        tinta_shared_graphics_uninitialize();
        std::cerr << "app initialization failed\n";
        return 1;
    }
    app.hwnd = CreateWindowExW(0, L"STATIC", L"layout tests",
                               WS_OVERLAPPEDWINDOW, 0, 0,
                               settings.width, settings.height,
                               nullptr, nullptr, app.instance, nullptr);
    app.dpi_scale = 1.0f;
    app.max_ast_nodes = 1000000;
    app.max_ast_depth = 256;
    app.max_mermaid_nodes = 10000;
    app.max_mermaid_edges = 20000;
    app.page_margin_left = 40.0f;
    app.page_margin_top = 20.0f;
    app.page_margin_right = 40.0f;
    app.page_margin_bottom = 40.0f;
    if (!tinta_app_update_formats(&app)) {
        std::cerr << "text format initialization failed\n";
        if (app.hwnd) DestroyWindow(app.hwnd);
        tinta_app_destroy(&app);
        tinta_shared_graphics_uninitialize();
        return 1;
    }

    test_inline_styles(app);
    test_block_collapse(app);
#if TINTA_ENABLE_MERMAID
    test_mermaid_layout(app);
#endif

    if (app.hwnd) DestroyWindow(app.hwnd);
    tinta_app_destroy(&app);
    tinta_shared_graphics_uninitialize();

    if (!failures) {
        std::cout << "Layout regression tests passed\n";
        return 0;
    }
    std::cerr << failures << " layout regression test(s) failed\n";
    return 1;
}
