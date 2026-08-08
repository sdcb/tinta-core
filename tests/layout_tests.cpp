#include <windows.h>

#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <iostream>
#include <string>

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

bool segment_crosses_run(const TintaDrawLine &line, const TintaTextRun &run) {
    float left = run.x + 1.0f;
    float right = run.x + run.width - 1.0f;
    float top = run.y + 1.0f;
    float bottom = run.y + run.height - 1.0f;
    float x0 = static_cast<float>(line.a.x);
    float y0 = static_cast<float>(line.a.y);
    float dx = static_cast<float>(line.b.x - line.a.x);
    float dy = static_cast<float>(line.b.y - line.a.y);
    float t0 = 0.0f;
    float t1 = 1.0f;
    auto clip = [&](float p, float q) {
        if (std::fabs(p) < 0.0001f) return q >= 0.0f;
        float value = q / p;
        if (p < 0.0f) {
            if (value > t1) return false;
            t0 = t0 > value ? t0 : value;
        } else {
            if (value < t0) return false;
            t1 = t1 < value ? t1 : value;
        }
        return true;
    };
    return right > left && bottom > top &&
           clip(-dx, x0 - left) && clip(dx, right - x0) &&
           clip(-dy, y0 - top) && clip(dy, bottom - y0) &&
           t0 <= t1;
}

bool connector_avoids_runs(TintaApp &app,
                           std::initializer_list<TintaTextRun *> runs) {
    for (size_t i = 0; i < app.lines.len; i++) {
        auto *line = TINTA_VEC_PTR(TintaDrawLine, app.lines, i);
        if (line->color != app.theme->quote) continue;
        for (auto *run : runs) {
            if (run && segment_crosses_run(*line, *run)) return false;
        }
    }
    for (size_t i = 0; i < app.paths.len; i++) {
        auto *path = TINTA_VEC_PTR(TintaDrawPath, app.paths, i);
        if (path->color != app.theme->quote || path->point_count < 4 ||
            path->point_offset + path->point_count > app.path_points.len)
            continue;
        auto *points = static_cast<D2D1_POINT_2F *>(app.path_points.data) +
                       path->point_offset;
        for (size_t segment = 0; segment + 3 < path->point_count;
             segment += 3) {
            for (int sample = 0; sample <= 32; sample++) {
                float t = static_cast<float>(sample) / 32.0f;
                float u = 1.0f - t;
                float x = u*u*u*points[segment].x +
                    3*u*u*t*points[segment+1].x +
                    3*u*t*t*points[segment+2].x +
                    t*t*t*points[segment+3].x;
                float y = u*u*u*points[segment].y +
                    3*u*u*t*points[segment+1].y +
                    3*u*t*t*points[segment+2].y +
                    t*t*t*points[segment+3].y;
                for (auto *run : runs) {
                    if (run && x > run->x + 1 &&
                        x < run->x + run->width - 1 &&
                        y > run->y + 1 &&
                        y < run->y + run->height - 1)
                        return false;
                }
            }
        }
    }
    return true;
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
        "**x^2^** and **bold ~~gone~~ ==mark==**\n\n"
        "**<sub>bold-sub</sub>** *<sup>italic-sup</sup>* "
        "***<sub>`both-code`</sub>*** "
        "[<sup>linked-sup</sup>](https://example.com/script)\n";
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
    auto *bold_sub = find_run(app, L"bold-sub");
    auto *italic_sup = find_run(app, L"italic-sup");
    auto *both_script_code = find_run(app, L"both-code");
    auto *linked_sup = find_run(app, L"linked-sup");

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
    check(bold_sub && bold_sub->layout &&
              bold_sub->layout->lpVtbl->GetFontWeight(bold_sub->layout) ==
                  TINTA_DWRITE_FONT_WEIGHT_BOLD &&
              std::fabs(bold_sub->layout->lpVtbl->GetFontSize(bold_sub->layout) -
                        app.small_format->lpVtbl->GetFontSize(app.small_format)) < 0.01f,
          "HTML sub preserves inherited bold at script size");
    check(italic_sup && italic_sup->layout &&
              italic_sup->layout->lpVtbl->GetFontStyle(italic_sup->layout) ==
                  TINTA_DWRITE_FONT_STYLE_ITALIC,
          "HTML sup preserves inherited italic");
    check(both_script_code && both_script_code->layout &&
              both_script_code->layout->lpVtbl->GetFontWeight(
                  both_script_code->layout) == TINTA_DWRITE_FONT_WEIGHT_BOLD &&
              both_script_code->layout->lpVtbl->GetFontStyle(
                  both_script_code->layout) == TINTA_DWRITE_FONT_STYLE_ITALIC &&
              std::fabs(both_script_code->layout->lpVtbl->GetFontSize(
                            both_script_code->layout) -
                        app.small_format->lpVtbl->GetFontSize(app.small_format)) < 0.01f,
          "inline code in HTML sub keeps script size and combined emphasis");
    check(linked_sup && linked_sup->url &&
              std::strcmp(linked_sup->url,
                          "https://example.com/script") == 0 &&
              linked_sup->underline && linked_sup->color == app.theme->link,
          "HTML sup preserves enclosing link semantics");
}

#if TINTA_ENABLE_SVG
void test_svg_layout(TintaApp &app) {
    const char *source =
        "![Vector](data:image/svg+xml,%3Csvg%20width%3D%27120%27%20height%3D%2760%27%20viewBox%3D%270%200%20120%2060%27%3E%3Crect%20width%3D%27120%27%20height%3D%2760%27/%3E%3C/svg%3E)";
    check(load(app, source), "SVG Data URI layout loads");
    check(app.svg_blocks.len == 1, "standalone SVG receives a block header");
    check(app.svgs.len == 1, "standalone SVG creates a vector draw item");
    check(app.image_resources.len == 1, "SVG counts as an image resource");
    if (app.image_resources.len == 1) {
        auto *resource = TINTA_VEC_PTR(
            TintaImageResource, app.image_resources, 0);
        check(resource->svg && resource->svg_source &&
                  std::fabs(resource->svg_width - 120.0f) < 0.01f &&
                  std::fabs(resource->svg_height - 60.0f) < 0.01f,
              "SVG source and natural size are retained");
    }
    if (app.svgs.len == 1) {
        auto *draw = TINTA_VEC_PTR(TintaDrawSvg, app.svgs, 0);
        check(draw->rect.right - draw->rect.left == 120 &&
                  draw->rect.bottom - draw->rect.top == 60,
              "standalone SVG keeps natural size and does not upscale");
    }
    if (app.svg_blocks.len == 1) {
        auto *block = TINTA_VEC_PTR(TintaSvgBlock, app.svg_blocks, 0);
        check(tinta_toggle_collapsible_at(
                  &app, block->rect.left + 24, block->rect.top + 16, false) &&
                  tinta_layout_document(&app),
              "SVG block uses animated-block collapse state");
        block = TINTA_VEC_PTR(TintaSvgBlock, app.svg_blocks, 0);
        check(block->expansion == 0.0f,
              "SVG block can collapse to its header");
    }

    check(load(app,
          "before ![Vector](data:image/svg+xml,%3Csvg%20width%3D%2720%27%20height%3D%2710%27%3E%3C/svg%3E) after"),
          "inline SVG layout loads");
    check(app.svg_blocks.len == 0,
          "inline SVG keeps ordinary image semantics without a header");
    check(app.svgs.len == 1, "inline SVG still creates a vector draw item");

    check(load(app,
          "![Bad](data:image/svg+xml,%3C!DOCTYPE%20svg%3E%3Csvg%3E%3C/svg%3E)"),
          "invalid SVG falls back during layout");
    auto *bad_link = find_run(app, L"Bad");
    check(app.svg_blocks.len == 0 && app.svgs.len == 0 &&
              bad_link && bad_link->url,
          "invalid standalone SVG becomes an alt-text link");
    check(load(app,
          "![](data:image/svg+xml,%3C!DOCTYPE%20svg%3E%3Csvg%3E%3C/svg%3E)"),
          "invalid SVG without alt text falls back");
    check(find_run(app, L"SVG image") != nullptr,
          "invalid Data URI avoids exposing the full URI");

#if TINTA_ENABLE_LOCAL_IMAGES
    wchar_t temporary_directory[MAX_PATH]{};
    wchar_t temporary_file[MAX_PATH]{};
    const char local_source[] =
        "<?xml version='1.0'?><svg width='48' height='24'></svg>";
    if (GetTempPathW(MAX_PATH, temporary_directory) &&
        swprintf_s(temporary_file, L"%stinta-svg-%lu.svg",
                   temporary_directory, GetCurrentProcessId()) > 0) {
        HANDLE file = CreateFileW(temporary_file, GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                                  nullptr);
        DWORD written = 0;
        bool wrote = file != INVALID_HANDLE_VALUE &&
            WriteFile(file, local_source,
                      static_cast<DWORD>(sizeof(local_source) - 1),
                      &written, nullptr) &&
            written == sizeof(local_source) - 1;
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        TintaStr8 path{};
        std::wstring uri = temporary_file;
        uri += L"?theme=dark#icon";
        size_t resource_index = SIZE_MAX;
        bool ready = false;
        bool loaded = wrote && tinta_utf16_to_utf8(
            uri.c_str(), uri.size(), &path) &&
            tinta_image_resource_get(&app, path.data, &resource_index,
                                     &ready);
        check(loaded && ready && resource_index < app.image_resources.len,
              "local SVG with query and fragment loads");
        if (loaded && resource_index < app.image_resources.len) {
            auto *resource = TINTA_VEC_PTR(
                TintaImageResource, app.image_resources, resource_index);
            check(resource->svg && resource->svg_source &&
                      resource->svg_width == 48 && resource->svg_height == 24,
                  "local SVG content and dimensions are retained");
        }
        tinta_str8_destroy(&path);
        DeleteFileW(temporary_file);
    } else {
        check(false, "temporary local SVG path created");
    }
#endif
}
#endif

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

void test_html_details(TintaApp &app) {
    const char *source =
        "<details>\n"
        "<summary>Outer <a href='https://example.com'>link</a> H<sub>2</sub>O</summary>\n\n"
        "Hidden paragraph.\n\n"
        "```cpp\n"
        "const char *hidden_code = \"details\";\n"
        "```\n\n"
        "<details open>\n"
        "<summary>Inner</summary>\n\n"
        "Nested visible body.\n\n"
        "</details>\n\n"
        "</details>\n";
    check(load(app, source), "HTML details document lays out");
    check(app.details_blocks.len == 2,
          "nested details create two disclosure blocks");
    auto *outer_text = find_run_containing(app, L"Outer");
    auto *link = find_run(app, L"link");
    auto *subscript = find_run(app, L"2");
    auto *hidden = find_run_containing(app, L"Hidden paragraph");
    auto *hidden_code = find_run_containing(app, L"hidden_code");
    auto *nested_body = find_run_containing(app, L"Nested visible body");
    TintaDetailsBlock *outer = nullptr;
    for (size_t i = 0; i < app.details_blocks.len; i++) {
        auto *candidate = TINTA_VEC_PTR(
            TintaDetailsBlock, app.details_blocks, i);
        if (outer_text && outer_text->y >= candidate->summary_rect.top &&
            outer_text->y < candidate->summary_rect.bottom) {
            outer = candidate;
            break;
        }
    }
    check(outer && outer->expansion < 0.001f,
          "details without open starts collapsed");
    check(outer_text && tinta_run_is_visually_exposed(&app, outer_text),
          "summary remains visible while collapsed");
    check(hidden && !tinta_run_is_visually_exposed(&app, hidden) &&
              hidden_code && !tinta_run_is_visually_exposed(&app, hidden_code) &&
              nested_body && !tinta_run_is_visually_exposed(&app, nested_body),
          "collapsed details hides all nested content");
    check(subscript && subscript->layout &&
              std::fabs(subscript->layout->lpVtbl->GetFontSize(
                            subscript->layout) -
                        app.small_format->lpVtbl->GetFontSize(app.small_format)) < 0.01f,
          "HTML sub uses the existing script format");
    if (link)
        check(!tinta_toggle_collapsible_at(
                  &app, (int)(link->x + 2), (int)(link->y + 2), false),
              "summary links take priority over disclosure toggling");
    if (!outer) return;
    const int summary_x = outer->summary_rect.left + 8;
    const int summary_y = (outer->summary_rect.top +
                           outer->summary_rect.bottom) / 2;
    check(tinta_toggle_collapsible_at(
              &app, summary_x, summary_y, true),
          "summary starts the disclosure animation");
    const ULONGLONG start = app.block_animation_tick;
    check(tinta_block_animation_tick(&app, start + 90) &&
              tinta_layout_document(&app),
          "details animation advances at its midpoint");
    outer_text = find_run_containing(app, L"Outer");
    outer = nullptr;
    for (size_t i = 0; i < app.details_blocks.len; i++) {
        auto *candidate = TINTA_VEC_PTR(
            TintaDetailsBlock, app.details_blocks, i);
        if (outer_text && outer_text->y >= candidate->summary_rect.top &&
            outer_text->y < candidate->summary_rect.bottom) outer = candidate;
    }
    check(outer && outer->expansion > 0.05f && outer->expansion < 0.95f,
          "details has a partial animated body height");
    check(tinta_block_animation_tick(&app, start + 180) &&
              tinta_block_animation_tick(&app, start + 270) &&
              tinta_block_animation_tick(&app, start + 360) &&
              tinta_layout_document(&app),
          "details animation completes");
    hidden = find_run_containing(app, L"Hidden paragraph");
    hidden_code = find_run_containing(app, L"hidden_code");
    nested_body = find_run_containing(app, L"Nested visible body");
    check(hidden && tinta_run_is_visually_exposed(&app, hidden) &&
              hidden_code && tinta_run_is_visually_exposed(&app, hidden_code) &&
              nested_body && tinta_run_is_visually_exposed(&app, nested_body),
          "expanded outer details reveals nested Markdown and code");
    if (hidden_code && hidden_code->horizontal_region < app.horizontal_regions.len) {
        auto *code_region = TINTA_VEC_PTR(
            TintaHorizontalRegion, app.horizontal_regions,
            hidden_code->horizontal_region);
        check(code_region->kind == TINTA_HORIZONTAL_CODE &&
                  code_region->parent_region != SIZE_MAX &&
                  TINTA_VEC_AT(TintaHorizontalRegion, app.horizontal_regions,
                               code_region->parent_region).kind ==
                      TINTA_HORIZONTAL_DETAILS,
              "code region retains its outer details clip parent");
    }
    outer_text = find_run_containing(app, L"Outer");
    outer = nullptr;
    for (size_t i = 0; i < app.details_blocks.len; i++) {
        auto *candidate = TINTA_VEC_PTR(
            TintaDetailsBlock, app.details_blocks, i);
        if (outer_text && outer_text->y >= candidate->summary_rect.top &&
            outer_text->y < candidate->summary_rect.bottom) outer = candidate;
    }
    if (outer) {
        check(tinta_toggle_collapsible_at(
                  &app, outer->summary_rect.left + 8,
                  (outer->summary_rect.top + outer->summary_rect.bottom) / 2,
                  false) && tinta_layout_document(&app),
              "expanded details collapses immediately when animations are off");
        nested_body = find_run_containing(app, L"Nested visible body");
        check(nested_body && tinta_expand_run_block(&app, nested_body) &&
                  tinta_layout_document(&app),
              "logical access expands all hidden details ancestors");
        nested_body = find_run_containing(app, L"Nested visible body");
        check(nested_body && tinta_run_is_visually_exposed(&app, nested_body),
              "ancestor expansion restores nested body geometry");
    }
}

#if TINTA_ENABLE_MERMAID
void test_mermaid_layout(TintaApp &app) {
    const char *source =
        "```mermaid\n"
        "flowchart LR\n"
        "A[Rect] --> B{Feature}\n"
        "B --> C{{Hex}}\n"
        "C -.-> D((Circle))\n"
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
    check(app.paths.len == 3,
          "Mermaid edges are stored as Direct2D cubic paths");
    if (app.paths.len == 3) {
        bool found_dashed = false;
        for (size_t i = 0; i < app.paths.len; i++) {
            auto *path = TINTA_VEC_PTR(TintaDrawPath, app.paths, i);
            check(path->point_count == 4 && path->horizontal_region == 0,
                  "ordinary Mermaid edges use one clipped cubic segment");
            found_dashed = found_dashed || path->dashed;
        }
        check(found_dashed, "dashed Mermaid edges retain their stroke style");
    }
    tinta_render(&app);
    check(app.render_target != nullptr,
          "Direct2D renders solid and dashed Mermaid cubic paths");

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

void test_mermaid_subgraph_layout(TintaApp &app) {
    const char *source =
        "```mermaid\n"
        "flowchart LR\n"
        "Start --> outer --> Finish\n"
        "subgraph outer [Pipeline]\n"
        "  direction TB\n"
        "  A --> inner\n"
        "  subgraph inner [Workers]\n"
        "    direction RL\n"
        "    B --> C\n"
        "  end\n"
        "  A --> B\n"
        "end\n"
        "```\n";
    check(load(app, source), "nested Mermaid subgraph document lays out");
    if (!app.layout_complete) return;
    auto *pipeline = find_run(app, L"Pipeline");
    auto *workers = find_run(app, L"Workers");
    auto *a = find_run(app, L"A");
    auto *b = find_run(app, L"B");
    auto *c = find_run(app, L"C");
    TintaDrawRect *outer = nullptr;
    TintaDrawRect *inner = nullptr;
    for (size_t i = 0; i < app.rects.len; i++) {
        auto *rect = TINTA_VEC_PTR(TintaDrawRect, app.rects, i);
        if (!rect->outline && rect->opacity > 0.37f &&
            rect->opacity < 0.39f) {
            if (!outer ||
                (rect->rect.right - rect->rect.left) *
                    (rect->rect.bottom - rect->rect.top) >
                (outer->rect.right - outer->rect.left) *
                    (outer->rect.bottom - outer->rect.top)) {
                inner = outer;
                outer = rect;
            } else {
                inner = rect;
            }
        }
    }
    check(outer && inner, "nested subgraph containers are rendered");
    if (outer && inner) {
        check(inner->rect.left >= outer->rect.left + 20 &&
                  inner->rect.right <= outer->rect.right - 20 &&
                  inner->rect.top >= outer->rect.top + 36 &&
                  inner->rect.bottom <= outer->rect.bottom - 20,
              "nested rendered group respects container padding");
        check(pipeline && std::fabs(
                  pipeline->x + pipeline->width * 0.5f -
                  (outer->rect.left + outer->rect.right) * 0.5f) < 2.0f,
              "outer subgraph title is centered");
        check(workers && std::fabs(
                  workers->x + workers->width * 0.5f -
                  (inner->rect.left + inner->rect.right) * 0.5f) < 2.0f,
              "inner subgraph title is centered");
    }
    check(a && b && c && a->y + a->height < b->y &&
              b->y + b->height < c->y,
          "member external links make nested group inherit parent TB direction");
    check(connector_avoids_runs(app, {a, b, c}),
          "Subgraph connector routing avoids unrelated node labels");
    if (pipeline) {
        size_t position = SIZE_MAX;
        tinta_hit_test(&app, pipeline->x + 1, pipeline->y + 1,
                       &position, nullptr);
        check(position >= pipeline->doc_start &&
                  position <= pipeline->doc_start + pipeline->doc_length,
              "subgraph title participates in text hit testing");
    }
    if (app.mermaid_blocks.len == 1) {
        auto *block = TINTA_VEC_PTR(TintaMermaidBlock,
                                    app.mermaid_blocks, 0);
        check(tinta_toggle_collapsible_at(
                  &app, block->rect.left + 40,
                  block->rect.top + 16, false) &&
                  tinta_layout_document(&app),
              "nested Subgraph Mermaid block collapses normally");
        pipeline = find_run(app, L"Pipeline");
        check(pipeline && !tinta_run_is_visually_exposed(&app, pipeline),
              "collapsed Mermaid hides Subgraph titles");
        check(pipeline && tinta_expand_run_block(&app, pipeline) &&
                  tinta_layout_document(&app),
              "Subgraph title access expands the Mermaid block");
        pipeline = find_run(app, L"Pipeline");
        check(pipeline && tinta_run_is_visually_exposed(&app, pipeline),
              "expanded Mermaid restores Subgraph title geometry");
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
    app.max_document_bytes = 64u * 1024u * 1024u;
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
    test_html_details(app);
#if TINTA_ENABLE_SVG
    test_svg_layout(app);
#endif
#if TINTA_ENABLE_MERMAID
    test_mermaid_layout(app);
    test_mermaid_subgraph_layout(app);
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
