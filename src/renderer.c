#include "app.h"
#include "features.h"
#if TINTA_ENABLE_SYNTAX
#include "syntax.h"
#endif

#include <ctype.h>
#include <commdlg.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <urlmon.h>
#include <wctype.h>

typedef struct InlineState {
    float left;
    float right;
    float x;
    float y;
    float line_height;
    float base_line_height;
} InlineState;

typedef struct InlineStyle {
    IDWriteTextFormat *format;
    uint32_t color;
    const char *url;
    bool underline;
    bool strikethrough;
    bool highlight;
    int script;
} InlineStyle;

#if TINTA_ENABLE_MERMAID
typedef struct ConnectorPath {
    D2D1_POINT_2F points[5];
    size_t count;
} ConnectorPath;
#endif

static float maxf(float a, float b) { return a > b ? a : b; }
static float minf(float a, float b) { return a < b ? a : b; }
static float scale(const TintaApp *app, float value) { return value * app->dpi_scale * app->zoom; }
static float ui_scale(const TintaApp *app, float value) { return value * app->dpi_scale; }
static float code_header_height(const TintaApp *app) { return scale(app, 32); }
static float viewport_x(const TintaApp *app) { (void)app; return 0; }
static float viewport_width(const TintaApp *app) {
    return (float)app->width;
}

static TintaHorizontalRegion *horizontal_region(TintaApp *app, size_t index) {
    if (!app || index == SIZE_MAX || index >= app->horizontal_regions.len)
        return NULL;
    return TINTA_VEC_PTR(TintaHorizontalRegion, app->horizontal_regions, index);
}

static const TintaHorizontalRegion *horizontal_region_const(
    const TintaApp *app, size_t index) {
    if (!app || index == SIZE_MAX || index >= app->horizontal_regions.len)
        return NULL;
    return TINTA_VEC_PTR(TintaHorizontalRegion, app->horizontal_regions, index);
}

static float horizontal_region_offset(const TintaHorizontalRegion *region) {
    return region && region->overflow ?
        (float)region->viewport.left - region->content_left - region->scroll_x :
        0;
}

static void track_horizontal_extent(TintaApp *app, float left, float right) {
    TintaHorizontalRegion *region = horizontal_region(
        app, app->active_horizontal_region);
    if (region) {
        region->content_left = minf(region->content_left, left);
        region->content_right = maxf(region->content_right, right);
    } else {
        app->content_width = maxf(app->content_width, right);
    }
}

static void save_horizontal_region_states(TintaApp *app) {
    size_t i;
    if (!app || !app->horizontal_regions.len) return;
    tinta_vec_clear(&app->horizontal_scroll_states);
    for (i = 0; i < app->horizontal_regions.len; i++) {
        const TintaHorizontalRegion *region = TINTA_VEC_PTR(
            TintaHorizontalRegion, app->horizontal_regions, i);
        TintaHorizontalScrollState state;
        state.kind = region->kind;
        state.source_offset = region->source_offset;
        state.ordinal = region->ordinal;
        state.scroll_dip = region->scroll_x /
            (region->scale_factor > 0 ? region->scale_factor : 1);
        tinta_vec_push(&app->horizontal_scroll_states, &state);
    }
}

void tinta_horizontal_region_clear_states(TintaApp *app) {
    if (!app) return;
    tinta_vec_clear(&app->horizontal_scroll_states);
    tinta_vec_clear(&app->horizontal_regions);
    app->active_horizontal_region = SIZE_MAX;
    app->hovered_horizontal_region = -1;
    app->dragging_horizontal_region = -1;
}

static size_t begin_horizontal_region(TintaApp *app,
                                      TintaHorizontalRegionKind kind,
                                      size_t source_offset, RECT viewport) {
    TintaHorizontalRegion region;
    size_t i;
    float scale_factor = app->dpi_scale * app->zoom;
    memset(&region, 0, sizeof(region));
    region.kind = kind;
    region.source_offset = source_offset;
    region.viewport = viewport;
    region.scale_factor = scale_factor > 0 ? scale_factor : 1;
    region.content_left = FLT_MAX;
    region.content_right = -FLT_MAX;
    for (i = 0; i < app->horizontal_regions.len; i++) {
        const TintaHorizontalRegion *other = TINTA_VEC_PTR(
            TintaHorizontalRegion, app->horizontal_regions, i);
        if (other->kind == kind && other->source_offset == source_offset)
            region.ordinal++;
    }
    for (i = 0; i < app->horizontal_scroll_states.len; i++) {
        const TintaHorizontalScrollState *state = TINTA_VEC_PTR(
            TintaHorizontalScrollState, app->horizontal_scroll_states, i);
        if (state->kind == kind && state->source_offset == source_offset &&
            state->ordinal == region.ordinal) {
            region.scroll_x = state->scroll_dip * scale_factor;
            break;
        }
    }
    if (!tinta_vec_push(&app->horizontal_regions, &region)) return SIZE_MAX;
    app->active_horizontal_region = app->horizontal_regions.len - 1;
    app->content_width = maxf(app->content_width, (float)viewport.right);
    return app->active_horizontal_region;
}

static void finish_horizontal_region(TintaApp *app, size_t index) {
    TintaHorizontalRegion *region = horizontal_region(app, index);
    float viewport_size;
    float content_size;
    float maximum;
    app->active_horizontal_region = SIZE_MAX;
    if (!region) return;
    if (region->content_left == FLT_MAX) {
        region->content_left = (float)region->viewport.left;
        region->content_right = (float)region->viewport.right;
    }
    viewport_size = maxf(1, (float)(region->viewport.right -
                                   region->viewport.left));
    content_size = maxf(0, region->content_right - region->content_left);
    region->overflow = content_size > viewport_size + 0.5f;
    maximum = maxf(0, content_size - viewport_size);
    region->scroll_x = minf(maxf(region->scroll_x, 0), maximum);
}

static uint64_t performance_time_us(void) {
    LARGE_INTEGER counter;
    static LONGLONG frequency;
    if (!frequency) {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        frequency = value.QuadPart;
    }
    QueryPerformanceCounter(&counter);
    return frequency > 0 ?
        (uint64_t)(counter.QuadPart * 1000000ULL / frequency) : 0;
}

bool tinta_vertical_scrollbar_geometry(const TintaApp *app,
                                       TintaScrollbarGeometry *geometry) {
    float visible = viewport_width(app);
    float scrollbar_size = ui_scale(app, 14);
    bool horizontal = app->content_width > visible;
    float track_length;
    float thumb_length;
    float maximum;
    if (!geometry || app->height <= 0 || app->content_height <= app->height)
        return false;
    track_length = app->height - (horizontal ? scrollbar_size : 0);
    if (track_length <= 0) return false;
    maximum = app->content_height - app->height;
    thumb_length = maxf(ui_scale(app, 30),
        track_length / app->content_height * track_length);
    thumb_length = minf(thumb_length, track_length);
    geometry->left = app->width - scrollbar_size;
    geometry->right = (float)app->width;
    geometry->top = maximum > 0 ? app->scroll_y / maximum *
        (track_length - thumb_length) : 0;
    geometry->bottom = geometry->top + thumb_length;
    geometry->track_length = track_length;
    geometry->maximum_scroll = maximum;
    return true;
}

bool tinta_horizontal_scrollbar_geometry(const TintaApp *app,
                                         TintaScrollbarGeometry *geometry) {
    float visible = viewport_width(app);
    float left_edge = viewport_x(app);
    float scrollbar_size = ui_scale(app, 14);
    bool vertical = app->content_height > app->height;
    float track_length;
    float thumb_length;
    float maximum;
    if (!geometry || visible <= 0 || app->content_width <= visible)
        return false;
    track_length = visible - (vertical ? scrollbar_size : 0);
    if (track_length <= 0) return false;
    maximum = app->content_width - visible;
    thumb_length = maxf(ui_scale(app, 30),
        track_length / app->content_width * track_length);
    thumb_length = minf(thumb_length, track_length);
    geometry->left = left_edge + (maximum > 0 ? app->scroll_x / maximum *
        (track_length - thumb_length) : 0);
    geometry->right = geometry->left + thumb_length;
    geometry->top = app->height - scrollbar_size;
    geometry->bottom = (float)app->height;
    geometry->track_length = track_length;
    geometry->maximum_scroll = maximum;
    return true;
}

static bool horizontal_region_scrollbar_geometry(
    const TintaApp *app, size_t index, TintaScrollbarGeometry *geometry) {
    const TintaHorizontalRegion *region = horizontal_region_const(app, index);
    float visible;
    float content;
    float track_left;
    float thumb_length;
    float travel;
    if (!geometry || !region || !region->overflow) return false;
    visible = (float)(region->viewport.right - region->viewport.left);
    content = region->content_right - region->content_left;
    if (visible <= 0 || content <= visible) return false;
    track_left = viewport_x(app) - app->scroll_x + region->viewport.left;
    thumb_length = maxf(ui_scale(app, 30), visible / content * visible);
    thumb_length = minf(thumb_length, visible);
    travel = visible - thumb_length;
    geometry->maximum_scroll = content - visible;
    geometry->left = track_left + (geometry->maximum_scroll > 0 ?
        region->scroll_x / geometry->maximum_scroll * travel : 0);
    geometry->right = geometry->left + thumb_length;
    geometry->top = region->viewport.bottom - app->scroll_y;
    geometry->bottom = geometry->top + ui_scale(app, 14);
    geometry->track_length = visible;
    return true;
}

static int horizontal_region_at(const TintaApp *app, int x, int y,
                                bool scrollbar_only) {
    size_t i;
    float document_x = x - viewport_x(app) + app->scroll_x;
    float document_y = y + app->scroll_y;
    for (i = app->horizontal_regions.len; i > 0; i--) {
        const TintaHorizontalRegion *region = TINTA_VEC_PTR(
            TintaHorizontalRegion, app->horizontal_regions, i - 1);
        if (!region->overflow) continue;
        if (scrollbar_only) {
            TintaScrollbarGeometry geometry;
            float track_left = viewport_x(app) - app->scroll_x +
                               region->viewport.left;
            if (horizontal_region_scrollbar_geometry(app, i - 1, &geometry) &&
                x >= track_left && x <= track_left + geometry.track_length &&
                y >= geometry.top && y <= geometry.bottom)
                return (int)(i - 1);
        } else if (document_x >= region->viewport.left &&
                   document_x <= region->viewport.right &&
                   document_y >= region->viewport.top &&
                   document_y <= region->viewport.bottom + ui_scale(app, 14)) {
            return (int)(i - 1);
        }
    }
    return -1;
}

static void set_scroll_y(TintaApp *app, float value) {
    float maximum = maxf(0, app->content_height - app->height);
    app->scroll_y = minf(maxf(value, 0), maximum);
    InvalidateRect(app->hwnd, NULL, FALSE);
}

bool tinta_scrollbar_update_hover(TintaApp *app, int x, int y) {
    TintaScrollbarGeometry vertical;
    TintaScrollbarGeometry horizontal;
    bool old_vertical = app->scrollbar_hovered;
    bool old_horizontal = app->h_scrollbar_hovered;
    int old_region = app->hovered_horizontal_region;
    app->hovered_horizontal_region = horizontal_region_at(app, x, y, true);
    app->scrollbar_hovered = tinta_vertical_scrollbar_geometry(app, &vertical) &&
        x >= vertical.left && x <= vertical.right &&
        y >= 0 && y <= vertical.track_length;
    app->h_scrollbar_hovered = tinta_horizontal_scrollbar_geometry(app, &horizontal) &&
        x >= viewport_x(app) && x <= viewport_x(app) + horizontal.track_length &&
        y >= horizontal.top && y <= horizontal.bottom;
    return old_vertical != app->scrollbar_hovered ||
        old_horizontal != app->h_scrollbar_hovered ||
        old_region != app->hovered_horizontal_region;
}

bool tinta_scrollbar_begin_drag(TintaApp *app, int x, int y) {
    TintaScrollbarGeometry geometry;
    tinta_scrollbar_update_hover(app, x, y);
    if (app->hovered_horizontal_region >= 0 &&
        horizontal_region_scrollbar_geometry(
            app, (size_t)app->hovered_horizontal_region, &geometry)) {
        TintaHorizontalRegion *region = horizontal_region(
            app, (size_t)app->hovered_horizontal_region);
        float track_left = viewport_x(app) - app->scroll_x +
                           region->viewport.left;
        float thumb_length = geometry.right - geometry.left;
        float travel = geometry.track_length - thumb_length;
        float local_x = x - track_left;
        float thumb_position = geometry.left - track_left;
        if ((x < geometry.left || x > geometry.right) && travel > 0) {
            thumb_position = minf(maxf(local_x - thumb_length * 0.5f, 0),
                                  travel);
            region->scroll_x = thumb_position / travel *
                               geometry.maximum_scroll;
            InvalidateRect(app->hwnd, NULL, FALSE);
        }
        app->dragging_horizontal_region = app->hovered_horizontal_region;
        app->horizontal_region_drag_start_x = (float)x;
        app->horizontal_region_drag_start_scroll = region->scroll_x;
        return true;
    }
    if (app->scrollbar_hovered &&
        tinta_vertical_scrollbar_geometry(app, &geometry)) {
        float thumb_length = geometry.bottom - geometry.top;
        float travel = geometry.track_length - thumb_length;
        if ((y < geometry.top || y > geometry.bottom) && travel > 0) {
            float thumb_position = minf(maxf(y - thumb_length * 0.5f, 0), travel);
            set_scroll_y(app, thumb_position / travel * geometry.maximum_scroll);
        }
        app->scrollbar_dragging = true;
        app->scrollbar_drag_start_y = (float)y;
        app->scrollbar_drag_start_scroll = app->scroll_y;
        return true;
    }
    if (app->h_scrollbar_hovered &&
        tinta_horizontal_scrollbar_geometry(app, &geometry)) {
        float thumb_length = geometry.right - geometry.left;
        float travel = geometry.track_length - thumb_length;
        float local_x = x - viewport_x(app);
        float thumb_position = geometry.left - viewport_x(app);
        if ((local_x < thumb_position || local_x > thumb_position + thumb_length) &&
            travel > 0) {
            thumb_position = minf(maxf(local_x - thumb_length * 0.5f, 0), travel);
            app->scroll_x = thumb_position / travel * geometry.maximum_scroll;
            InvalidateRect(app->hwnd, NULL, FALSE);
        }
        app->h_scrollbar_dragging = true;
        app->h_scrollbar_drag_start_x = (float)x;
        app->h_scrollbar_drag_start_scroll = app->scroll_x;
        return true;
    }
    return false;
}

bool tinta_scrollbar_drag(TintaApp *app, int x, int y) {
    TintaScrollbarGeometry geometry;
    app->mouse_x = x;
    app->mouse_y = y;
    if (app->dragging_horizontal_region >= 0 &&
        horizontal_region_scrollbar_geometry(
            app, (size_t)app->dragging_horizontal_region, &geometry)) {
        TintaHorizontalRegion *region = horizontal_region(
            app, (size_t)app->dragging_horizontal_region);
        float travel = geometry.track_length -
                       (geometry.right - geometry.left);
        if (region && travel > 0) {
            region->scroll_x = app->horizontal_region_drag_start_scroll +
                (x - app->horizontal_region_drag_start_x) / travel *
                geometry.maximum_scroll;
            region->scroll_x = minf(maxf(region->scroll_x, 0),
                                    geometry.maximum_scroll);
            InvalidateRect(app->hwnd, NULL, FALSE);
        }
        return true;
    }
    if (app->scrollbar_dragging &&
        tinta_vertical_scrollbar_geometry(app, &geometry)) {
        float travel = geometry.track_length - (geometry.bottom - geometry.top);
        if (travel > 0)
            set_scroll_y(app, app->scrollbar_drag_start_scroll +
                (y - app->scrollbar_drag_start_y) / travel * geometry.maximum_scroll);
        return true;
    }
    if (app->h_scrollbar_dragging &&
        tinta_horizontal_scrollbar_geometry(app, &geometry)) {
        float travel = geometry.track_length - (geometry.right - geometry.left);
        if (travel > 0) {
            app->scroll_x = app->h_scrollbar_drag_start_scroll +
                (x - app->h_scrollbar_drag_start_x) / travel * geometry.maximum_scroll;
            app->scroll_x = minf(maxf(app->scroll_x, 0), geometry.maximum_scroll);
            InvalidateRect(app->hwnd, NULL, FALSE);
        }
        return true;
    }
    return false;
}

bool tinta_scrollbar_end_drag(TintaApp *app, int x, int y) {
    bool was_dragging = app->scrollbar_dragging || app->h_scrollbar_dragging ||
                        app->dragging_horizontal_region >= 0;
    app->scrollbar_dragging = false;
    app->h_scrollbar_dragging = false;
    app->dragging_horizontal_region = -1;
    tinta_scrollbar_update_hover(app, x, y);
    if (was_dragging) InvalidateRect(app->hwnd, NULL, FALSE);
    return was_dragging;
}

bool tinta_horizontal_region_scroll_at(TintaApp *app, int x, int y,
                                       float amount) {
    int index = horizontal_region_at(app, x, y, false);
    TintaHorizontalRegion *region;
    float maximum;
    if (index < 0) return false;
    region = horizontal_region(app, (size_t)index);
    if (!region) return false;
    maximum = maxf(0, region->content_right - region->content_left -
        (region->viewport.right - region->viewport.left));
    region->scroll_x = minf(maxf(region->scroll_x + amount, 0), maximum);
    InvalidateRect(app->hwnd, NULL, FALSE);
    return true;
}

bool tinta_horizontal_region_scroll_run_into_view(TintaApp *app,
                                                   const TintaTextRun *run) {
    TintaHorizontalRegion *region;
    float visible;
    float maximum;
    float run_left;
    float run_right;
    if (!app || !run) return false;
    region = horizontal_region(app, run->horizontal_region);
    if (!region || !region->overflow) return false;
    visible = (float)(region->viewport.right - region->viewport.left);
    maximum = maxf(0, region->content_right - region->content_left - visible);
    run_left = run->x - region->content_left;
    run_right = run_left + run->width;
    if (run_left < region->scroll_x)
        region->scroll_x = run_left;
    else if (run_right > region->scroll_x + visible)
        region->scroll_x = run_right - visible;
    region->scroll_x = minf(maxf(region->scroll_x, 0), maximum);
    return true;
}

static float format_line_height(IDWriteTextFormat *format) {
    return format ? format->lpVtbl->GetFontSize(format) * 1.7f : 0.0f;
}

void tinta_layout_clear(TintaApp *app) {
    size_t i;
    save_horizontal_region_states(app);
    for (i = 0; i < app->text_runs.len; i++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun, app->text_runs, i);
        if (run->layout) run->layout->lpVtbl->Release(run->layout);
        free(run->text); free(run->url);
    }
    for (i = 0; i < app->headings.len; i++) {
        TintaHeading *heading = TINTA_VEC_PTR(TintaHeading, app->headings, i);
        free(heading->text);
        free(heading->slug);
    }
    for (i = 0; i < app->code_blocks.len; i++) {
        TintaCodeBlock *block = TINTA_VEC_PTR(TintaCodeBlock, app->code_blocks, i);
        free(block->text);
        free(block->language);
    }
    for (i = 0; i < app->mermaid_blocks.len; i++) {
        TintaMermaidBlock *block = TINTA_VEC_PTR(
            TintaMermaidBlock, app->mermaid_blocks, i);
        free(block->text);
    }
    tinta_vec_clear(&app->text_runs); tinta_vec_clear(&app->rects);
    tinta_vec_clear(&app->lines); tinta_vec_clear(&app->headings);
    tinta_vec_clear(&app->scroll_anchors);
    tinta_vec_clear(&app->hit_entries);
    app->hit_index_dirty = true;
    tinta_vec_clear(&app->bitmaps); tinta_vec_clear(&app->code_blocks);
    tinta_vec_clear(&app->mermaid_blocks); tinta_str16_clear(&app->doc_text);
    tinta_vec_clear(&app->horizontal_regions);
    app->active_horizontal_region = SIZE_MAX;
    app->hovered_horizontal_region = -1;
    app->dragging_horizontal_region = -1;
    app->hovered_code_block = -1;
    app->hovered_mermaid_block = -1;
    app->notice_kind = TINTA_NOTICE_NONE;
    app->notice_code_block = -1;
    app->notice_mermaid_block = -1;
    if (app->hwnd) KillTimer(app->hwnd, TINTA_TIMER_NOTIFICATION);
}

static void capture_scroll_anchor(TintaApp *app) {
    size_t i;
    const TintaScrollAnchor *chosen = NULL;
    if (!app || app->scroll_anchor_pending || app->scroll_y <= 0)
        return;
    for (i = 0; i < app->scroll_anchors.len; i++) {
        const TintaScrollAnchor *anchor = TINTA_VEC_PTR(
            TintaScrollAnchor, app->scroll_anchors, i);
        if (anchor->rendered_y > app->scroll_y) break;
        chosen = anchor;
    }
    if (chosen) {
        app->scroll_anchor_pending = true;
        app->pending_scroll_source_offset = chosen->source_offset;
        app->pending_scroll_delta = app->scroll_y - chosen->rendered_y;
    }
}

static void restore_scroll_anchor(TintaApp *app, bool layout_complete) {
    size_t i;
    const TintaScrollAnchor *chosen = NULL;
    if (!app || !app->scroll_anchor_pending) return;
    for (i = 0; i < app->scroll_anchors.len; i++) {
        const TintaScrollAnchor *anchor = TINTA_VEC_PTR(
            TintaScrollAnchor, app->scroll_anchors, i);
        if (anchor->source_offset > app->pending_scroll_source_offset) break;
        chosen = anchor;
    }
    if (chosen && (layout_complete ||
        app->scroll_anchors.len &&
        TINTA_VEC_AT(TintaScrollAnchor, app->scroll_anchors,
                     app->scroll_anchors.len - 1).source_offset >=
            app->pending_scroll_source_offset)) {
        app->scroll_y = maxf(0, chosen->rendered_y +
                                  app->pending_scroll_delta);
        app->scroll_anchor_pending = false;
    } else if (layout_complete) {
        app->scroll_anchor_pending = false;
    }
}

static void apply_font_fallback(TintaApp *app, IDWriteTextLayout *layout) {
    if (app->font_fallback) {
        IDWriteTextLayout2 *layout2 = NULL;
        if (SUCCEEDED(layout->lpVtbl->QueryInterface(
                layout, &TINTA_IID_IDWRITE_TEXT_LAYOUT2, (void **)&layout2))) {
            layout2->lpVtbl->SetFontFallback(layout2, app->font_fallback);
            layout2->lpVtbl->Release(layout2);
        }
    }
}

static bool create_text_layout(TintaApp *app, IDWriteTextFormat *format,
                               const wchar_t *text, size_t length, float max_width,
                               IDWriteTextLayout **layout,
                               TintaDWriteTextMetrics *metrics) {
    HRESULT hr;
    if (length > UINT32_MAX) return false;
    *layout = NULL;
    hr = app->dwrite_factory->lpVtbl->CreateTextLayout(
        app->dwrite_factory, text, (UINT32)length, format, max_width, 100000.0f,
        layout);
    if (FAILED(hr) || !*layout) return false;
    apply_font_fallback(app, *layout);
    memset(metrics, 0, sizeof(*metrics));
    hr = (*layout)->lpVtbl->GetMetrics(*layout, metrics);
    if (FAILED(hr)) {
        (*layout)->lpVtbl->Release(*layout);
        *layout = NULL;
        return false;
    }
    return true;
}

static SIZE measure(TintaApp *app, IDWriteTextFormat *format,
                    const wchar_t *text, size_t length) {
    SIZE size = {0, 0};
    IDWriteTextLayout *layout = NULL;
    TintaDWriteTextMetrics metrics;
    if (create_text_layout(app, format, text ? text : L"", length,
                           100000.0f, &layout, &metrics)) {
        size.cx = (LONG)ceilf(metrics.widthIncludingTrailingWhitespace);
        size.cy = (LONG)ceilf(metrics.height);
        layout->lpVtbl->Release(layout);
    }
    return size;
}

static SIZE measure_wrapped(TintaApp *app, IDWriteTextFormat *format,
                            const wchar_t *text, size_t length,
                            float max_width) {
    SIZE size = {0, 0};
    IDWriteTextLayout *layout = NULL;
    TintaDWriteTextMetrics metrics;
    if (create_text_layout(app, format, text ? text : L"", length,
                           maxf(1, max_width), &layout, &metrics)) {
        layout->lpVtbl->SetWordWrapping(layout, TINTA_DWRITE_WORD_WRAPPING_WRAP);
        layout->lpVtbl->GetMetrics(layout, &metrics);
        size.cx = (LONG)ceilf(metrics.widthIncludingTrailingWhitespace);
        size.cy = (LONG)ceilf(metrics.height);
        layout->lpVtbl->Release(layout);
    }
    return size;
}

static bool append_run_sized_ex(TintaApp *app, const wchar_t *text, size_t length,
                                IDWriteTextFormat *format, uint32_t color,
                                float x, float y, float max_width,
                                bool wrap, const char *url, bool append_document,
                                TintaTextRun **created) {
    TintaTextRun run;
    TintaDWriteTextMetrics metrics;
    size_t old_document_length = app->doc_text.len;
    memset(&run, 0, sizeof(run));
    run.text = tinta_wcsdup_n(text, length);
    run.url = tinta_str8_dup(url ? url : "", url ? strlen(url) : 0);
    if (!run.text || !run.url) { free(run.text); free(run.url); return false; }
    if (!create_text_layout(app, format, run.text, length, max_width,
                            &run.layout, &metrics)) {
        free(run.text); free(run.url); return false;
    }
    if (wrap) {
        run.layout->lpVtbl->SetWordWrapping(
            run.layout, TINTA_DWRITE_WORD_WRAPPING_WRAP);
        if (FAILED(run.layout->lpVtbl->GetMetrics(run.layout, &metrics))) {
            run.layout->lpVtbl->Release(run.layout);
            free(run.text);
            free(run.url);
            return false;
        }
    }
    run.text_length = length; run.color = color; run.opacity = 1.0f;
    run.x = x; run.y = y;
    run.width = metrics.widthIncludingTrailingWhitespace;
    run.height = metrics.height;
    run.doc_start = app->doc_text.len; run.doc_length = append_document ? length : 0;
    run.horizontal_region = app->active_horizontal_region;
    if ((append_document && !tinta_str16_append(&app->doc_text, text, length)) ||
        !tinta_vec_push(&app->text_runs, &run)) {
        if (append_document) {
            app->doc_text.len = old_document_length;
            if (app->doc_text.data) app->doc_text.data[old_document_length] = 0;
        }
        run.layout->lpVtbl->Release(run.layout);
        free(run.text); free(run.url); return false;
    }
    track_horizontal_extent(app, x, x + run.width);
    app->hit_index_dirty = true;
    if (created) *created = TINTA_VEC_PTR(TintaTextRun, app->text_runs, app->text_runs.len - 1);
    return true;
}

static bool append_run_ex(TintaApp *app, const wchar_t *text, size_t length,
                          IDWriteTextFormat *format, uint32_t color, float x, float y,
                          const char *url, bool append_document,
                          TintaTextRun **created) {
    return append_run_sized_ex(app, text, length, format, color, x, y,
                               100000.0f, false, url, append_document, created);
}

static bool append_run(TintaApp *app, const wchar_t *text, size_t length,
                       IDWriteTextFormat *format, uint32_t color, float x, float y,
                       const char *url, TintaTextRun **created) {
    return append_run_ex(app, text, length, format, color, x, y, url, true, created);
}

static bool append_run_width(TintaApp *app, const wchar_t *text, size_t length,
                             IDWriteTextFormat *format, uint32_t color,
                             float x, float y, float max_width,
                             TintaTextRun **created) {
    return append_run_sized_ex(app, text, length, format, color, x, y,
                               max_width, true, NULL, true, created);
}

static bool add_rect(TintaApp *app, float left, float top, float right, float bottom,
                     uint32_t color, float radius, bool outline, float stroke) {
    TintaDrawRect item;
    item.rect.left = (LONG)left; item.rect.top = (LONG)top;
    item.rect.right = (LONG)right; item.rect.bottom = (LONG)bottom;
    item.color = color; item.opacity = 1.0f; item.radius = radius;
    item.outline = outline; item.stroke = stroke; item.shape = 0;
    item.horizontal_region = app->active_horizontal_region;
    track_horizontal_extent(app, left, right);
    return tinta_vec_push(&app->rects, &item) != NULL;
}

static bool add_rect_alpha(TintaApp *app, float left, float top,
                           float right, float bottom, uint32_t color,
                           float opacity, float radius, bool outline,
                           float stroke) {
    TintaDrawRect *item;
    if (!add_rect(app, left, top, right, bottom, color, radius,
                  outline, stroke)) return false;
    item = TINTA_VEC_PTR(TintaDrawRect, app->rects, app->rects.len - 1);
    item->opacity = opacity;
    return true;
}

#if TINTA_ENABLE_MERMAID
static bool add_shape(TintaApp *app, float left, float top, float right, float bottom,
                      uint32_t color, float opacity, TintaDrawShape shape,
                      bool outline, float stroke) {
    TintaDrawRect item;
    item.rect.left=(LONG)left; item.rect.top=(LONG)top; item.rect.right=(LONG)right; item.rect.bottom=(LONG)bottom;
    item.color=color; item.opacity=opacity;
    item.radius=shape==TINTA_DRAW_SHAPE_STADIUM?(bottom-top)*.5f:scale(app,6);
    item.outline=outline; item.stroke=stroke; item.shape=shape;
    item.horizontal_region = app->active_horizontal_region;
    track_horizontal_extent(app, left, right);
    return tinta_vec_push(&app->rects,&item)!=NULL;
}
#endif

static bool add_line(TintaApp *app, float x1, float y1, float x2, float y2,
                     uint32_t color, float stroke) {
    TintaDrawLine item;
    item.a.x = (LONG)x1; item.a.y = (LONG)y1; item.b.x = (LONG)x2; item.b.y = (LONG)y2;
    item.color = color; item.stroke = stroke; item.opacity = 1.0f; item.dashed = false;
    item.horizontal_region = app->active_horizontal_region;
    track_horizontal_extent(app, minf(x1, x2), maxf(x1, x2));
    return tinta_vec_push(&app->lines, &item) != NULL;
}

#if TINTA_ENABLE_MERMAID
static bool add_connector_line(TintaApp *app, float x1, float y1, float x2, float y2,
                               uint32_t color, float stroke, bool dashed) {
    TintaDrawLine item;
    item.a.x = (LONG)x1; item.a.y = (LONG)y1;
    item.b.x = (LONG)x2; item.b.y = (LONG)y2;
    item.color = color; item.stroke = stroke; item.opacity = 1.0f;
    item.dashed = dashed;
    item.horizontal_region = app->active_horizontal_region;
    track_horizontal_extent(app, minf(x1, x2), maxf(x1, x2));
    return tinta_vec_push(&app->lines, &item) != NULL;
}
#endif


static bool add_line_alpha(TintaApp *app, float x1, float y1, float x2, float y2,
                           uint32_t color, float stroke, float opacity) {
    TintaDrawLine item;
    item.a.x = (LONG)x1; item.a.y = (LONG)y1;
    item.b.x = (LONG)x2; item.b.y = (LONG)y2;
    item.color = color; item.stroke = stroke; item.opacity = opacity; item.dashed = false;
    item.horizontal_region = app->active_horizontal_region;
    track_horizontal_extent(app, minf(x1, x2), maxf(x1, x2));
    return tinta_vec_push(&app->lines, &item) != NULL;
}

static bool flatten(const TintaElement *element, TintaStr8 *output) {
    size_t i;
    if (element->type == TINTA_ELEMENT_TEXT) return tinta_str8_append(output, element->text, strlen(element->text));
    if (element->type == TINTA_ELEMENT_MERMAID_DIAGRAM && element->text && element->text[0])
        return tinta_str8_append(output, element->text, strlen(element->text));
    if (element->type == TINTA_ELEMENT_SOFT_BREAK) return tinta_str8_append_char(output, ' ');
    if (element->type == TINTA_ELEMENT_HARD_BREAK) return tinta_str8_append_char(output, '\n');
    for (i = 0; i < element->child_count; i++) if (!flatten(element->children[i], output)) return false;
    return true;
}

static bool flatten_ruby(const TintaElement *element, TintaStr8 *base,
                         TintaStr8 *annotation) {
    size_t i;
    if (element->type == TINTA_ELEMENT_RUBY_TEXT)
        return flatten(element, annotation);
    if (element->type == TINTA_ELEMENT_TEXT)
        return tinta_str8_append(base, element->text, strlen(element->text));
    for (i = 0; i < element->child_count; i++)
        if (!flatten_ruby(element->children[i], base, annotation)) return false;
    return true;
}

static bool text_clusters(TintaApp *app, IDWriteTextFormat *format,
                          const wchar_t *text, size_t length,
                          TintaDWriteClusterMetrics **clusters,
                          UINT32 *count) {
    IDWriteTextLayout *layout = NULL;
    TintaDWriteTextMetrics text_metrics;
    *clusters = NULL;
    *count = 0;
    if (!create_text_layout(app, format, text, length, 100000.0f,
                            &layout, &text_metrics)) return false;
    layout->lpVtbl->GetClusterMetrics(layout, NULL, 0, count);
    if (!*count) {
        layout->lpVtbl->Release(layout);
        return true;
    }
    *clusters = (TintaDWriteClusterMetrics *)calloc(*count,
                                                    sizeof(**clusters));
    if (!*clusters || FAILED(layout->lpVtbl->GetClusterMetrics(
            layout, *clusters, *count, count))) {
        free(*clusters);
        *clusters = NULL;
        *count = 0;
        layout->lpVtbl->Release(layout);
        return false;
    }
    layout->lpVtbl->Release(layout);
    return true;
}

static bool add_wrapped(TintaApp *app, InlineState *state, const wchar_t *text,
                        size_t length, const InlineStyle *style) {
    size_t position = 0;
    while (position < length) {
        size_t newline = position;
        size_t line_position;
        UINT32 cluster_index = 0;
        UINT32 cluster_count = 0;
        TintaDWriteClusterMetrics *clusters = NULL;
        while (newline < length && text[newline] != L'\n') newline++;
        if (newline == position) {
            const wchar_t nl = L'\n';
            if (!tinta_str16_append(&app->doc_text, &nl, 1)) return false;
            state->x = state->left; state->y += state->line_height;
            state->line_height = state->base_line_height; position++; continue;
        }
        if (!text_clusters(app, style->format, text + position,
                           newline - position, &clusters, &cluster_count))
            return false;
        line_position = position;
        while (cluster_index < cluster_count) {
            UINT32 scan = cluster_index;
            UINT32 natural_break = cluster_index;
            size_t use = 0;
            float width = 0;
            float max_width = maxf(1, state->right - state->x);
            TintaTextRun *run = NULL;
            float offset = style->script < 0 ? -state->base_line_height * 0.18f :
                           style->script > 0 ? state->base_line_height * 0.28f : 0;
            while (scan < cluster_count &&
                   width + clusters[scan].width <= max_width) {
                width += clusters[scan].width;
                if (clusters[scan].canWrapLineAfter) natural_break = scan + 1;
                scan++;
            }
            if (scan == cluster_index && state->x > state->left) {
                state->x = state->left;
                state->y += state->line_height;
                state->line_height = state->base_line_height;
                continue;
            }
            if (scan == cluster_index) scan++;
            if (scan < cluster_count && natural_break > cluster_index)
                scan = natural_break;
            {
                UINT32 index;
                for (index = cluster_index; index < scan; index++)
                    use += clusters[index].length;
            }
            if (!append_run(app, text + line_position, use,
                            style->format, style->color, state->x,
                            state->y + offset, style->url, &run)) {
                free(clusters);
                return false;
            }
            run->underline = style->underline;
            run->strikethrough = style->strikethrough;
            if (style->highlight)
                add_rect(app, run->x - scale(app, 1), run->y,
                         run->x + run->width + scale(app, 1), run->y + run->height,
                         app->theme->dark ? 0x665A18 : 0xFFF0A6,
                         scale(app, 2), false, 0);
            state->line_height = maxf(state->line_height, run->height + fabsf(offset));
            line_position += use;
            cluster_index = scan;
            if (cluster_index < cluster_count) {
                while (cluster_index < cluster_count &&
                       clusters[cluster_index].isWhitespace) {
                    line_position += clusters[cluster_index].length;
                    cluster_index++;
                }
                state->x = state->left; state->y += state->line_height;
                state->line_height = state->base_line_height;
            } else state->x += run->width;
        }
        free(clusters);
        position = newline;
        if (position < length && text[position] == L'\n') {
            const wchar_t nl = L'\n';
            if (!tinta_str16_append(&app->doc_text, &nl, 1)) return false;
            state->x = state->left;
            state->y += state->line_height;
            state->line_height = state->base_line_height;
            position++;
        }
    }
    return true;
}

static bool layout_image(TintaApp *app, const TintaElement *element,
                         float *y, float left, float right);

static bool layout_inline(TintaApp *app, const TintaElement *element, InlineState *state,
                          InlineStyle style) {
    size_t i;
    if (element->type == TINTA_ELEMENT_STRONG) style.format = app->bold_format;
    if (element->type == TINTA_ELEMENT_EMPHASIS) style.format = app->italic_format;
    if (element->type == TINTA_ELEMENT_LINK) {
        style.color = app->theme->link;
        style.url = element->url;
        style.underline = true;
    }
    if (element->type == TINTA_ELEMENT_HIGHLIGHT) style.highlight = true;
    if (element->type == TINTA_ELEMENT_STRIKETHROUGH) style.strikethrough = true;
    if (element->type == TINTA_ELEMENT_SUPERSCRIPT) {
        style.format = app->small_format;
        style.script = -1;
    }
    if (element->type == TINTA_ELEMENT_SUBSCRIPT) {
        style.format = app->small_format;
        style.script = 1;
    }
    if (element->type == TINTA_ELEMENT_RUBY) {
        TintaStr8 base8 = {0}, annotation8 = {0};
        TintaStr16 base16 = {0}, annotation16 = {0};
        SIZE base_size, annotation_size, space_size;
        float width, annotation_x, base_x, annotation_y;
        TintaTextRun *base_run = NULL;
        bool ok = flatten_ruby(element, &base8, &annotation8) &&
            tinta_utf8_to_utf16(base8.data, base8.len, &base16) &&
            tinta_utf8_to_utf16(annotation8.data, annotation8.len, &annotation16);
        if (!ok) goto ruby_done;
        base_size = measure(app, style.format, base16.data, base16.len);
        annotation_size = measure(app, app->small_format,
                                  annotation16.data, annotation16.len);
        space_size = measure(app, style.format, L" ", 1);
        width = (float)max(base_size.cx, annotation_size.cx);
        if (state->x + width > state->right && state->x > state->left) {
            state->x = state->left;
            state->y += state->line_height;
            state->line_height = state->base_line_height;
        }
        if (state->x > state->left) {
            state->x = state->left;
            state->y += state->line_height;
            state->line_height = state->base_line_height;
        }
        annotation_y = state->y;
        annotation_x = state->x + (width - annotation_size.cx) * 0.5f;
        base_x = state->x + (width - base_size.cx) * 0.5f;
        if (annotation16.len && !append_run_ex(
                app, annotation16.data, annotation16.len, app->small_format,
                app->theme->syntax_comment, annotation_x, annotation_y,
                NULL, false, NULL)) {
            ok = false;
            goto ruby_done;
        }
        state->y += annotation_size.cy;
        if (!append_run(app, base16.data, base16.len, style.format, style.color,
                        base_x, state->y, style.url, &base_run)) {
            ok = false;
            goto ruby_done;
        }
        base_run->underline = style.underline;
        base_run->strikethrough = style.strikethrough;
        state->line_height = maxf(state->base_line_height, (float)base_size.cy);
        state->x += width + space_size.cx;
ruby_done:
        tinta_str8_destroy(&base8); tinta_str8_destroy(&annotation8);
        tinta_str16_destroy(&base16); tinta_str16_destroy(&annotation16);
        return ok;
    }
    if (element->type == TINTA_ELEMENT_IMAGE) {
        state->x = state->left;
        if (!layout_image(app, element, &state->y, state->left, state->right)) return false;
        state->line_height = state->base_line_height;
        return true;
    }
    if (element->type == TINTA_ELEMENT_CODE) {
        TintaStr8 utf8 = {0};
        TintaStr16 wide = {0};
        SIZE code_size;
        SIZE space_size;
        float vertical_offset;
        bool result;
        if (!flatten(element, &utf8) ||
            !tinta_utf8_to_utf16(utf8.data, utf8.len, &wide)) {
            tinta_str8_destroy(&utf8); tinta_str16_destroy(&wide); return false;
        }
        code_size = measure(app, app->code_format, wide.data, wide.len);
        space_size = measure(app, style.format, L" ", 1);
        if (state->x + code_size.cx > state->right && state->x > state->left) {
            state->x = state->left;
            state->y += state->line_height;
            state->line_height = state->base_line_height;
        }
        add_rect(app, state->x - scale(app, 2), state->y,
                 state->x + code_size.cx + scale(app, 4),
                 state->y + state->base_line_height,
                 app->theme->code_background, scale(app, 2), false, 0);
        vertical_offset = (state->base_line_height -
                           app->code_format->lpVtbl->GetFontSize(app->code_format) * 1.2f) * 0.5f;
        result = append_run(app, wide.data, wide.len, app->code_format,
                            app->theme->code, state->x,
                            state->y + maxf(0, vertical_offset), style.url, NULL);
        if (result) state->x += code_size.cx + space_size.cx;
        tinta_str8_destroy(&utf8); tinta_str16_destroy(&wide);
        return result;
    }
    if (element->type == TINTA_ELEMENT_SOFT_BREAK) {
        wchar_t space = L' ';
        return add_wrapped(app, state, &space, 1, &style);
    }
    if (element->type == TINTA_ELEMENT_HARD_BREAK) {
        wchar_t newline = L'\n';
        return add_wrapped(app, state, &newline, 1, &style);
    }
    if (element->type == TINTA_ELEMENT_TEXT) {
        TintaStr16 wide = {0};
        bool result = tinta_utf8_to_utf16(element->text, strlen(element->text), &wide) &&
                      add_wrapped(app, state, wide.data, wide.len, &style);
        tinta_str16_destroy(&wide); return result;
    }
    for (i = 0; i < element->child_count; i++)
        if (!layout_inline(app, element->children[i], state, style)) return false;
    return true;
}

static bool layout_element(TintaApp *, const TintaElement *, float *, float, float);

static bool layout_children(TintaApp *app, const TintaElement *element,
                            float *y, float left, float right) {
    size_t i;
    for (i = 0; i < element->child_count; i++)
        if (!layout_element(app, element->children[i], y, left, right)) return false;
    return true;
}

static bool layout_paragraph(TintaApp *app, const TintaElement *element,
                             float *y, float left, float right) {
    float line_height = format_line_height(app->body_format);
    InlineState state = {left, right, left, *y, line_height, line_height};
    InlineStyle style = {app->body_format, app->theme->text, NULL, false, false, false, 0};
    size_t i;
    for (i = 0; i < element->child_count; i++)
        if (!layout_inline(app, element->children[i], &state, style)) return false;
    *y = state.y + state.line_height + scale(app, 14);
    return tinta_str16_append(&app->doc_text, L"\n\n", 2);
}

static wchar_t *heading_slug(TintaApp *app, const wchar_t *text, size_t length) {
    wchar_t *base;
    wchar_t *result;
    size_t i, out = 0, suffix = 0;
    bool pending_dash = false;
    base = (wchar_t *)calloc(length + 2, sizeof(*base));
    if (!base) return NULL;
    for (i = 0; i < length; i++) {
        wchar_t c = text[i];
        if (iswspace(c) || c == L'-') {
            if (out) pending_dash = true;
            continue;
        }
        if ((c < 128 && !iswalnum(c)) || (c >= 128 && iswpunct(c))) continue;
        if (pending_dash && out) base[out++] = L'-';
        pending_dash = false;
        base[out++] = towlower(c);
    }
    if (!out) base[out++] = L's';
    base[out] = 0;
    result = tinta_wcsdup_n(base, out);
    if (!result) { free(base); return NULL; }
    for (;;) {
        bool duplicate = false;
        for (i = 0; i < app->headings.len; i++) {
            TintaHeading *heading = TINTA_VEC_PTR(TintaHeading, app->headings, i);
            if (heading->slug && !wcscmp(heading->slug, result)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) break;
        free(result);
        suffix++;
        {
            wchar_t number[32];
            size_t number_length;
            _snwprintf_s(number, 32, _TRUNCATE, L"-%zu", suffix);
            number_length = wcslen(number);
            result = (wchar_t *)malloc((out + number_length + 1) * sizeof(*result));
            if (!result) { free(base); return NULL; }
            memcpy(result, base, out * sizeof(*result));
            memcpy(result + out, number, (number_length + 1) * sizeof(*result));
        }
    }
    free(base);
    return result;
}

static bool layout_heading(TintaApp *app, const TintaElement *element,
                           float *y, float left, float right) {
    TintaStr8 utf8 = {0}; TintaStr16 wide = {0}; TintaHeading heading;
    InlineState state;
    int level = element->level < 1 ? 1 : element->level > 6 ? 6 : element->level;
    bool ok = flatten(element, &utf8) && tinta_utf8_to_utf16(utf8.data, utf8.len, &wide);
    if (ok) {
        float line_height;
        heading.doc_start = app->doc_text.len;
        *y += scale(app, level == 1 ? 16.0f : 20.0f);
        line_height = format_line_height(app->heading_formats[level - 1]);
        state.left = state.x = left; state.right = right; state.y = *y;
        state.line_height = line_height; state.base_line_height = line_height;
        {
            InlineStyle style = {app->heading_formats[level - 1], app->theme->heading,
                                 NULL, false, false, false, 0};
            ok = add_wrapped(app, &state, wide.data, wide.len, &style);
        }
        *y = state.y + state.line_height;
        heading.text = tinta_wcsdup_n(wide.data, wide.len);
        heading.slug = heading_slug(app, wide.data, wide.len);
        heading.level = level; heading.y = state.y;
        heading.doc_length = app->doc_text.len - heading.doc_start;
        if (!heading.text || !heading.slug || !tinta_vec_push(&app->headings, &heading)) {
            free(heading.text); free(heading.slug); ok = false;
        }
        if (level <= 2) {
            float stroke = scale(app, level == 1 ? 2.0f : 1.0f);
            *y += scale(app, 6);
            add_line_alpha(app, left, *y, right, *y, app->theme->heading,
                           stroke, 0.3f);
            *y += stroke;
        }
        *y += scale(app, 12);
        if (!tinta_str16_append(&app->doc_text, L"\n\n", 2)) ok = false;
    }
    tinta_str8_destroy(&utf8); tinta_str16_destroy(&wide); return ok;
}

static bool layout_code(TintaApp *app, const TintaElement *element,
                        float *y, float left, float right) {
    TintaStr8 utf8 = {0}; TintaStr16 wide = {0};
    TintaStr16 language_label = {0};
    const char *language_utf8 = element->language && element->language[0] ?
        element->language : "Plain text";
    float top = *y, line_height = scale(app, 20), padding = scale(app, 12);
    float header_height = code_header_height(app);
    float block_right = left + maxf(right - left, scale(app, 240));
    bool ok = flatten(element, &utf8) &&
              tinta_utf8_to_utf16(utf8.data, utf8.len, &wide) &&
              tinta_utf8_to_utf16(language_utf8, strlen(language_utf8),
                                  &language_label);
    if (ok) {
        size_t line_start=0, line_count=1, rect_index, block_index;
        size_t region_index;
        size_t display_length = wide.len;
        float line_y=top+header_height+padding;
        float block_height;
#if TINTA_ENABLE_SYNTAX
        int language=tinta_syntax_language(element->language);
        bool block=false;
        TintaVec tokens;
        tinta_vec_init(&tokens,sizeof(TintaSyntaxToken));
#endif
        size_t scan;
        if (display_length && wide.data[display_length - 1] == L'\n') {
            display_length--;
            if (display_length && wide.data[display_length - 1] == L'\r')
                display_length--;
        }
        for (scan = 0; scan < display_length; scan++)
            if (wide.data[scan] == L'\n') line_count++;
        block_height = header_height + line_count * line_height + padding * 2;
        rect_index = app->rects.len;
        if (!add_rect(app, left, top, block_right, top + block_height,
                      app->theme->code_background, 0, false, 0)) ok = false;
        {
            TintaCodeBlock command;
            command.rect.left = (LONG)left;
            command.rect.top = (LONG)top;
            command.rect.right = (LONG)block_right;
            command.rect.bottom = (LONG)(top + block_height);
            command.text = tinta_wcsdup_n(wide.data, wide.len);
            command.language = tinta_wcsdup_n(
                language_label.data, language_label.len);
            command.horizontal_region = SIZE_MAX;
            block_index = app->code_blocks.len;
            if (!command.text || !command.language ||
                !tinta_vec_push(&app->code_blocks, &command)) {
                free(command.text);
                free(command.language);
                ok = false;
            }
        }
        {
            RECT viewport = {
                (LONG)(left + padding),
                (LONG)(top + header_height),
                (LONG)(block_right - padding),
                (LONG)(top + block_height)
            };
            region_index = begin_horizontal_region(
                app, TINTA_HORIZONTAL_CODE, element->source_offset, viewport);
            if (region_index == SIZE_MAX) ok = false;
            else if (block_index < app->code_blocks.len)
                TINTA_VEC_PTR(TintaCodeBlock, app->code_blocks,
                              block_index)->horizontal_region = region_index;
        }
        while (ok && line_start <= display_length) {
            size_t line_end = line_start;
            float x = left + padding;
            while (line_end < display_length && wide.data[line_end] != L'\n')
                line_end++;
#if TINTA_ENABLE_SYNTAX
            size_t token_index;
            if (!tinta_syntax_tokenize(wide.data + line_start,
                                       line_end - line_start, language,
                                       &block, &tokens)) {
                ok = false;
                break;
            }
            for (token_index = 0; token_index < tokens.len; token_index++) {
                TintaSyntaxToken token = TINTA_VEC_AT(
                    TintaSyntaxToken, tokens, token_index);
                TintaTextRun *run = NULL;
                uint32_t color = app->theme->code;
                switch (token.type) {
                    case TINTA_SYNTAX_KEYWORD: color = app->theme->syntax_keyword; break;
                    case TINTA_SYNTAX_STRING: color = app->theme->syntax_string; break;
                    case TINTA_SYNTAX_COMMENT: color = app->theme->syntax_comment; break;
                    case TINTA_SYNTAX_NUMBER: color = app->theme->syntax_number; break;
                    case TINTA_SYNTAX_FUNCTION: color = app->theme->syntax_function; break;
                    case TINTA_SYNTAX_TYPE: color = app->theme->syntax_type; break;
                    case TINTA_SYNTAX_CONTROL: color = app->theme->syntax_control; break;
                    case TINTA_SYNTAX_OPERATOR: color = app->theme->accent; break;
                    default: break;
                }
                if (!append_run(app, wide.data + line_start + token.start,
                                token.length, app->code_format, color, x,
                                line_y, NULL, &run)) {
                    ok = false;
                    break;
                }
                x += run->width;
            }
#else
            {
                TintaTextRun *run = NULL;
                if (!append_run(app, wide.data + line_start,
                                line_end - line_start, app->code_format,
                                app->theme->code, x, line_y, NULL, &run)) {
                    ok = false;
                    break;
                }
                x += run->width;
            }
#endif
            if (!ok) break;
            line_y += line_height;
            if (line_end == display_length) break;
            if (!tinta_str16_append(&app->doc_text, L"\n", 1)) {
                ok = false;
                break;
            }
            line_start = line_end + 1;
        }
#if TINTA_ENABLE_SYNTAX
        tinta_vec_destroy(&tokens);
#endif
        finish_horizontal_region(app, region_index);
        if (ok && region_index < app->horizontal_regions.len) {
            TintaHorizontalRegion *region = TINTA_VEC_PTR(
                TintaHorizontalRegion, app->horizontal_regions, region_index);
            if (region->overflow) {
                float scrollbar_height = ui_scale(app, 14);
                block_height += scrollbar_height;
                region->viewport.bottom = (LONG)(top + block_height -
                                                  scrollbar_height);
            }
            TintaDrawRect *background = TINTA_VEC_PTR(TintaDrawRect, app->rects, rect_index);
            TintaCodeBlock *command = TINTA_VEC_PTR(TintaCodeBlock, app->code_blocks, block_index);
            background->rect.bottom = (LONG)(top + block_height);
            command->rect.bottom = (LONG)(top + block_height);
        }
        if (ok) ok = tinta_str16_append(&app->doc_text, L"\n\n", 2);
        *y = top + block_height + scale(app, 14);
    }
    tinta_str8_destroy(&utf8); tinta_str16_destroy(&wide);
    tinta_str16_destroy(&language_label); return ok;
}

static bool layout_list(TintaApp *app, const TintaElement *element,
                        float *y, float left, float right) {
    size_t i;
    int number = element->start;
    for (i = 0; i < element->child_count; i++) {
        const TintaElement *item = element->children[i];
        wchar_t marker[32];
        float item_start = *y;
        float line_height = format_line_height(app->body_format);
        float item_left = left + scale(app, 24);
        InlineState state = {item_left, right, item_left, *y,
                             line_height, line_height};
        bool inline_active = false;
        size_t child;
        if (item->task)
            wcscpy_s(marker, 32, item->task_checked ? L"\x2611 " : L"\x2610 ");
        else if (element->ordered)
            _snwprintf_s(marker, 32, _TRUNCATE, L"%d. ", number++);
        else
            wcscpy_s(marker, 32, L"• ");
        if (!append_run(app, marker, wcslen(marker), app->body_format, app->theme->text,
                        left, *y, NULL, NULL)) return false;
        for (child = 0; child < item->child_count; child++) {
            const TintaElement *content = item->children[child];
            if (content->type == TINTA_ELEMENT_PARAGRAPH) {
                size_t inline_index;
                if (inline_active) {
                    *y = state.y + state.line_height;
                    if (!tinta_str16_append(&app->doc_text, L"\n", 1))
                        return false;
                    inline_active = false;
                }
                state = (InlineState){item_left, right, item_left, *y,
                                      line_height, line_height};
                for (inline_index = 0; inline_index < content->child_count;
                     inline_index++) {
                    InlineStyle style = {app->body_format, app->theme->text,
                                         NULL, false, false, false, 0};
                    if (!layout_inline(app, content->children[inline_index],
                                       &state, style)) return false;
                }
                *y = state.y + state.line_height + scale(app, 4);
                if (!tinta_str16_append(&app->doc_text, L"\n", 1))
                    return false;
            } else if (content->type == TINTA_ELEMENT_TEXT ||
                       content->type == TINTA_ELEMENT_CODE ||
                       content->type == TINTA_ELEMENT_EMPHASIS ||
                       content->type == TINTA_ELEMENT_STRONG ||
                       content->type == TINTA_ELEMENT_LINK ||
                       content->type == TINTA_ELEMENT_IMAGE ||
                       content->type == TINTA_ELEMENT_SOFT_BREAK ||
                       content->type == TINTA_ELEMENT_HARD_BREAK ||
                       content->type == TINTA_ELEMENT_RUBY ||
                       content->type == TINTA_ELEMENT_HIGHLIGHT ||
                       content->type == TINTA_ELEMENT_SUPERSCRIPT ||
                       content->type == TINTA_ELEMENT_SUBSCRIPT ||
                       content->type == TINTA_ELEMENT_STRIKETHROUGH) {
                InlineStyle style = {app->body_format, app->theme->text,
                                     NULL, false, false, false, 0};
                if (!inline_active) {
                    state = (InlineState){item_left, right, item_left, *y,
                                          line_height, line_height};
                    inline_active = true;
                }
                if (!layout_inline(app, content, &state, style)) return false;
            } else {
                if (inline_active) {
                    *y = state.y + state.line_height + scale(app, 4);
                    if (!tinta_str16_append(&app->doc_text, L"\n", 1))
                        return false;
                    inline_active = false;
                }
                if (!layout_element(app, content, y, item_left, right)) return false;
            }
        }
        if (inline_active) {
            *y = state.y + state.line_height;
            if (!tinta_str16_append(&app->doc_text, L"\n", 1)) return false;
        }
        *y = maxf(*y, item_start + scale(app, 28));
        if ((!app->doc_text.len ||
             app->doc_text.data[app->doc_text.len - 1] != L'\n') &&
            !tinta_str16_append(&app->doc_text, L"\n", 1)) return false;
        if (!element->tight &&
            !tinta_str16_append(&app->doc_text, L"\n", 1)) return false;
    }
    *y += scale(app, 8);
    return true;
}

static bool layout_table(TintaApp *app, const TintaElement *element,
                         float *y, float left, float right) {
    size_t r, c, columns = element->col_count > 0 ? (size_t)element->col_count : 1;
    float *desired = (float *)calloc(columns, sizeof(*desired));
    float *minimum = (float *)calloc(columns, sizeof(*minimum));
    float *widths = (float *)calloc(columns, sizeof(*widths));
    float available = right - left;
    float desired_sum = 0, minimum_sum = 0, table_width;
    bool ok = desired && minimum && widths;
    if (!ok) goto done;
    for (c = 0; c < columns; c++) {
        desired[c] = scale(app, 60);
        minimum[c] = scale(app, 50);
    }
    for (r = 0; r < element->child_count; r++) {
        const TintaElement *row = element->children[r];
        for (c = 0; c < row->child_count && c < columns; c++) {
            TintaStr8 utf8 = {0};
            TintaStr16 wide = {0};
            IDWriteTextFormat *format = r ? app->body_format : app->bold_format;
            if (flatten(row->children[c], &utf8) &&
                tinta_utf8_to_utf16(utf8.data, utf8.len, &wide)) {
                SIZE measured = measure(app, format, wide.data, wide.len);
                float natural = measured.cx + scale(app, 14);
                desired[c] = maxf(desired[c], natural);
                minimum[c] = maxf(minimum[c], minf(natural, scale(app, 140)));
            }
            tinta_str8_destroy(&utf8);
            tinta_str16_destroy(&wide);
        }
    }
    for (c = 0; c < columns; c++) {
        desired_sum += desired[c];
        minimum_sum += minimum[c];
    }
    table_width = maxf(available, minimum_sum);
    if (desired_sum <= table_width) {
        float extra = (table_width - desired_sum) / columns;
        for (c = 0; c < columns; c++) widths[c] = desired[c] + extra;
    } else {
        float shrinkable = desired_sum - minimum_sum;
        float target = table_width - minimum_sum;
        for (c = 0; c < columns; c++) {
            float share = shrinkable > 0 ? (desired[c] - minimum[c]) / shrinkable : 0;
            widths[c] = minimum[c] + maxf(0, target) * share;
        }
    }
    app->content_width = maxf(app->content_width, left + table_width);
    for (r = 0; r < element->child_count; r++) {
        const TintaElement *row = element->children[r];
        float row_top=*y, row_bottom=row_top+scale(app,36);
        float x = left;
        for (c = 0; c < row->child_count && c < columns; c++) {
            float cell_width = widths[c];
            float line_height = format_line_height(r ? app->body_format : app->bold_format);
            float inner_left = x + scale(app,7);
            float inner_right = x + cell_width - scale(app,7);
            float content_x = inner_left;
            TintaStr8 utf8 = {0};
            TintaStr16 wide = {0};
            IDWriteTextFormat *format = r ? app->body_format : app->bold_format;
            if (row->children[c]->align == 2 || row->children[c]->align == 3) {
                if (flatten(row->children[c], &utf8) &&
                    tinta_utf8_to_utf16(utf8.data, utf8.len, &wide)) {
                    SIZE measured = measure(app, format, wide.data, wide.len);
                    float content_width = minf((float)measured.cx, inner_right - inner_left);
                    if (row->children[c]->align == 2)
                        content_x = inner_left + (inner_right - inner_left - content_width) * 0.5f;
                    else content_x = inner_right - content_width;
                }
                tinta_str8_destroy(&utf8);
                tinta_str16_destroy(&wide);
            }
            InlineState state={inner_left,inner_right,content_x,row_top+scale(app,7),line_height,line_height};
            size_t child;
            for(child=0;child<row->children[c]->child_count;child++)
                {
                    InlineStyle style = {r?app->body_format:app->bold_format,
                                         app->theme->text,NULL,false,false,false,0};
                    if(!layout_inline(app,row->children[c]->children[child],&state,
                                      style)) {
                        ok = false;
                        goto done;
                    }
                }
            row_bottom=maxf(row_bottom,state.y+state.line_height+scale(app,7));
            if (c + 1 < columns &&
                !tinta_str16_append(&app->doc_text, L"\t", 1)) {
                ok = false;
                goto done;
            }
            x += cell_width;
        }
        x = left;
        if (r > 0 && (r & 1) &&
            !add_rect_alpha(app, left, row_top, left + table_width, row_bottom,
                            app->theme->code_background, 0.18f, 0,
                            false, 0)) {
            ok = false;
            goto done;
        }
        for(c=0;c<columns;c++) {
            if (!add_rect(app,x,row_top,x+widths[c],row_bottom,
                          app->theme->quote,0,true,1)) {
                ok = false;
                goto done;
            }
            x += widths[c];
        }
        if (!tinta_str16_append(&app->doc_text, L"\n", 1)) {
            ok = false;
            goto done;
        }
        *y=row_bottom;
    }
    *y += scale(app, 12);
    if (!tinta_str16_append(&app->doc_text, L"\n", 1)) ok = false;
done:
    free(desired); free(minimum); free(widths);
    return ok;
}

#if TINTA_ENABLE_MERMAID
static bool rects_overlap(D2D1_RECT_F a, D2D1_RECT_F b) {
    return a.left < b.right && a.right > b.left &&
           a.top < b.bottom && a.bottom > b.top;
}

static bool mermaid_label_collides(const TintaMermaidLayout *graph,
                                    float offset, float top,
                                    const TintaVec *labels,
                                    D2D1_RECT_F candidate) {
    size_t i;
    for (i = 0; i < graph->node_count; i++) {
        D2D1_RECT_F node = {
            graph->nodes[i].left + offset,
            graph->nodes[i].top + top,
            graph->nodes[i].right + offset,
            graph->nodes[i].bottom + top
        };
        if (rects_overlap(candidate, node)) return true;
    }
    for (i = 0; i < labels->len; i++) {
        if (rects_overlap(candidate,
                          TINTA_VEC_AT(D2D1_RECT_F, *labels, i))) return true;
    }
    return false;
}
#endif

static bool layout_image_placeholder(TintaApp *app, const TintaElement *element,
                                     float *y, float left) {
    TintaStr8 alt8 = {0};
    TintaStr16 text = {0};
    TintaTextRun *prefix_run = NULL;
    TintaTextRun *link_run = NULL;
    const wchar_t prefix[] = L"Image: ";
    bool ok = flatten(element, &alt8);
    if (ok && alt8.len)
        ok = tinta_utf8_to_utf16(alt8.data, alt8.len, &text);
    else if (ok && element->url && element->url[0])
        ok = tinta_utf8_to_utf16(element->url, strlen(element->url), &text);
    else if (ok)
        ok = tinta_str16_assign(&text, L"image", 5);
    if (ok) {
        ok = append_run(app, prefix, 7, app->italic_format,
                        app->theme->syntax_comment, left, *y, NULL,
                        &prefix_run);
    }
    if (ok) {
        float x = left + (prefix_run ? prefix_run->width : 0);
        const char *url = element->url && element->url[0] ?
                          element->url : NULL;
        ok = append_run(app, text.data, text.len, app->italic_format,
                        url ? app->theme->link : app->theme->syntax_comment,
                        x, *y, url, &link_run);
        if (link_run) link_run->underline = url != NULL;
    }
    if (ok) {
        float height = prefix_run ? prefix_run->height : 0;
        if (link_run) height = maxf(height, link_run->height);
        *y += height + scale(app, 12);
    }
    tinta_str8_destroy(&alt8);
    tinta_str16_destroy(&text);
    return ok;
}

static bool layout_image(TintaApp *app, const TintaElement *element, float *y, float left, float right) {
#if TINTA_ENABLE_IMAGES
    size_t resource_index = 0;
    TintaImageResource *resource;
    TintaDrawBitmap item;
    float ratio;
    bool ready = false;
    if (!element->url) return layout_image_placeholder(app, element, y, left);
    if (!tinta_image_resource_get(app, element->url,
                                  &resource_index, &ready))
        return false;
    if (!ready || resource_index >= app->image_resources.len)
        return layout_image_placeholder(app, element, y, left);
    resource = TINTA_VEC_PTR(
        TintaImageResource, app->image_resources, resource_index);
    if (!resource->source || !resource->width || !resource->height)
        return layout_image_placeholder(app, element, y, left);
    ratio = minf(1, (right - left) / resource->width);
    item.resource_index = resource_index;
    item.rect.left = (LONG)left; item.rect.top = (LONG)*y;
    item.rect.right = (LONG)(left + resource->width * ratio);
    item.rect.bottom = (LONG)(*y + resource->height * ratio);
    if (!tinta_vec_push(&app->bitmaps, &item)) return false;
    app->content_width = maxf(app->content_width, (float)item.rect.right);
    *y = (float)item.rect.bottom + scale(app, 16); return true;
#else
    (void)right;
    return layout_image_placeholder(app, element, y, left);
#endif
}

#if TINTA_ENABLE_MERMAID
static bool layout_mermaid(TintaApp *app, const TintaElement *element,
                           float *y, float left, float right) {
    TintaStr8 source={0};
    TintaMermaidParseResult parsed;
    const TintaMermaidParseResult *cached;
    TintaMermaidSize *sizes = NULL; TintaMermaidLayout graph; size_t i; float top = *y;
    size_t region_index = SIZE_MAX;
    size_t mermaid_block_index = SIZE_MAX;
    TintaVec label_boxes;
    memset(&graph, 0, sizeof(graph));
    if (!tinta_vec_init(&label_boxes, sizeof(D2D1_RECT_F))) return false;
    if(!flatten(element,&source)) {
        tinta_vec_destroy(&label_boxes);
        return false;
    }
    cached = tinta_app_mermaid_parse(app, element, source.data, source.len);
    if (!cached) goto failed;
    parsed = *cached;
    if (!parsed.success) {
        bool fallback;
        app->focus_mermaid_on_next_layout = false;
        tinta_str8_destroy(&source);
        tinta_vec_destroy(&label_boxes);
        fallback = layout_code(app, element, y, left, right);
        return fallback;
    }
    sizes = (TintaMermaidSize *)calloc(parsed.diagram.node_count, sizeof(*sizes));
    if (!sizes) goto failed;
    for (i = 0; i < parsed.diagram.node_count; i++) {
        TintaStr16 wide = {0};
        SIZE natural;
        SIZE wrapped;
        float label_width;
        tinta_utf8_to_utf16(parsed.diagram.nodes[i].label, strlen(parsed.diagram.nodes[i].label), &wide);
        natural = measure(app, app->body_format, wide.data, wide.len);
        label_width = minf(maxf((float)natural.cx, scale(app, 60)),
                           scale(app, 240));
        wrapped = measure_wrapped(app, app->body_format, wide.data, wide.len,
                                  label_width);
        sizes[i].width = maxf(scale(app, 90), label_width + scale(app, 30));
        sizes[i].height = maxf(scale(app, 48), wrapped.cy + scale(app, 20));
        if (parsed.diagram.nodes[i].shape == TINTA_MERMAID_CIRCLE ||
            parsed.diagram.nodes[i].shape == TINTA_MERMAID_DIAMOND) {
            float side = maxf(sizes[i].width, sizes[i].height);
            sizes[i].width = side;
            sizes[i].height = side;
        }
        tinta_str16_destroy(&wide);
    }
    graph = tinta_mermaid_layout(&parsed.diagram, sizes, parsed.diagram.node_count,
                                  scale(app, 28), scale(app, 90));
    if (graph.node_count != parsed.diagram.node_count) goto failed;
    {
        float block_top = top;
        float block_right = left + maxf(right - left, scale(app, 240));
        top += code_header_height(app);
        RECT viewport = {(LONG)left, (LONG)top, (LONG)block_right, (LONG)top};
        float offset;
        region_index = begin_horizontal_region(
            app, TINTA_HORIZONTAL_MERMAID, element->source_offset, viewport);
        if (region_index == SIZE_MAX) goto failed;
        {
            TintaStr16 wide_source = {0};
            TintaMermaidBlock block;
            if (!tinta_utf8_to_utf16(source.data, source.len, &wide_source))
                goto failed;
            block.rect.left = (LONG)left;
            block.rect.top = (LONG)block_top;
            block.rect.right = (LONG)block_right;
            block.rect.bottom = (LONG)top;
            block.text = tinta_wcsdup_n(wide_source.data, wide_source.len);
            block.horizontal_region = region_index;
            tinta_str16_destroy(&wide_source);
            if (!block.text ||
                !tinta_vec_push(&app->mermaid_blocks, &block)) {
                free(block.text);
                goto failed;
            }
            mermaid_block_index = app->mermaid_blocks.len - 1;
        }
        offset = left + maxf(0, (block_right - left - graph.width) * 0.5f);
        float diagram_bottom = top + graph.height;
        size_t exterior_lane = 0;
        for (i = 0; i < parsed.diagram.edge_count; i++) {
            const TintaMermaidEdge *edge = &parsed.diagram.edges[i];
            TintaMermaidRect a = graph.nodes[edge->from], b = graph.nodes[edge->to];
            bool horizontal=parsed.diagram.direction==TINTA_MERMAID_LEFT_TO_RIGHT||parsed.diagram.direction==TINTA_MERMAID_RIGHT_TO_LEFT;
            ConnectorPath path = {0};
            float from_center_x, from_center_y, to_center_x, to_center_y;
            bool self_loop = edge->from == edge->to;
            bool skips_ranks = false;
            size_t p;
            a.left += offset; a.right += offset; a.top += top; a.bottom += top;
            b.left += offset; b.right += offset; b.top += top; b.bottom += top;
            from_center_x=(a.left+a.right)*.5f;from_center_y=(a.top+a.bottom)*.5f;
            to_center_x=(b.left+b.right)*.5f;to_center_y=(b.top+b.bottom)*.5f;
            if(graph.ranks&&edge->from<graph.rank_count&&edge->to<graph.rank_count){
                size_t from_rank=graph.ranks[edge->from],to_rank=graph.ranks[edge->to];
                skips_ranks=(from_rank>to_rank?from_rank-to_rank:to_rank-from_rank)>1;
            }
            if(horizontal){
                bool left_to_right=parsed.diagram.direction==TINTA_MERMAID_LEFT_TO_RIGHT;
                bool forward=!self_loop&&!skips_ranks&&
                    (left_to_right?to_center_x>from_center_x:to_center_x<from_center_x);
                if(self_loop){
                    float lane=scale(app,36+(float)exterior_lane++*14);
                    path.points[0]=(D2D1_POINT_2F){from_center_x,a.bottom};
                    path.points[1]=(D2D1_POINT_2F){from_center_x,a.bottom+lane};
                    path.points[2]=(D2D1_POINT_2F){a.right+lane,a.bottom+lane};
                    path.points[3]=(D2D1_POINT_2F){a.right+lane,from_center_y};
                    path.points[4]=(D2D1_POINT_2F){a.right,from_center_y};path.count=5;
                }else if(forward){
                    float start_x=left_to_right?a.right:a.left,end_x=left_to_right?b.left:b.right;
                    float middle_x=(start_x+end_x)*.5f;
                    path.points[0]=(D2D1_POINT_2F){start_x,from_center_y};
                    path.points[1]=(D2D1_POINT_2F){middle_x,from_center_y};
                    path.points[2]=(D2D1_POINT_2F){middle_x,to_center_y};
                    path.points[3]=(D2D1_POINT_2F){end_x,to_center_y};path.count=4;
                }else{
                    float lane=scale(app,36+(float)exterior_lane++*14);
                    float lane_y=maxf(a.bottom,b.bottom)+lane;
                    path.points[0]=(D2D1_POINT_2F){from_center_x,a.bottom};
                    path.points[1]=(D2D1_POINT_2F){from_center_x,lane_y};
                    path.points[2]=(D2D1_POINT_2F){to_center_x,lane_y};
                    path.points[3]=(D2D1_POINT_2F){to_center_x,b.bottom};path.count=4;
                }
            }else{
                bool top_to_bottom=parsed.diagram.direction==TINTA_MERMAID_TOP_TO_BOTTOM;
                bool forward=!self_loop&&!skips_ranks&&
                    (top_to_bottom?to_center_y>from_center_y:to_center_y<from_center_y);
                if(self_loop){
                    float lane=scale(app,36+(float)exterior_lane++*14);
                    path.points[0]=(D2D1_POINT_2F){a.right,from_center_y};
                    path.points[1]=(D2D1_POINT_2F){a.right+lane,from_center_y};
                    path.points[2]=(D2D1_POINT_2F){a.right+lane,a.bottom+lane};
                    path.points[3]=(D2D1_POINT_2F){from_center_x,a.bottom+lane};
                    path.points[4]=(D2D1_POINT_2F){from_center_x,a.bottom};path.count=5;
                }else if(forward){
                    float start_y=top_to_bottom?a.bottom:a.top,end_y=top_to_bottom?b.top:b.bottom;
                    float middle_y=(start_y+end_y)*.5f;
                    path.points[0]=(D2D1_POINT_2F){from_center_x,start_y};
                    path.points[1]=(D2D1_POINT_2F){from_center_x,middle_y};
                    path.points[2]=(D2D1_POINT_2F){to_center_x,middle_y};
                    path.points[3]=(D2D1_POINT_2F){to_center_x,end_y};path.count=4;
                }else{
                    float lane=scale(app,36+(float)exterior_lane++*14);
                    float lane_x=maxf(a.right,b.right)+lane;
                    path.points[0]=(D2D1_POINT_2F){a.right,from_center_y};
                    path.points[1]=(D2D1_POINT_2F){lane_x,from_center_y};
                    path.points[2]=(D2D1_POINT_2F){lane_x,to_center_y};
                    path.points[3]=(D2D1_POINT_2F){b.right,to_center_y};path.count=4;
                }
            }
            for(p=1;p<path.count;p++){
                if(!add_connector_line(app,path.points[p-1].x,path.points[p-1].y,
                        path.points[p].x,path.points[p].y,app->theme->quote,
                        edge->stroke_scale*1.5f,edge->dashed))goto failed;
                diagram_bottom=maxf(diagram_bottom,maxf(path.points[p-1].y,path.points[p].y));
            }
            if(edge->directed) {
                D2D1_POINT_2F tip=path.points[path.count-1],previous=path.points[path.count-2];
                float angle=atan2f(tip.y-previous.y,tip.x-previous.x),arrow=scale(app,9);
                add_line(app,tip.x,tip.y,tip.x-cosf(angle-.5f)*arrow,tip.y-sinf(angle-.5f)*arrow,app->theme->quote,1.5f);
                add_line(app,tip.x,tip.y,tip.x-cosf(angle+.5f)*arrow,tip.y-sinf(angle+.5f)*arrow,app->theme->quote,1.5f);
            }
            if(edge->label && edge->label[0]) {
                TintaStr16 label={0}; SIZE label_size;
                size_t middle=path.count/2;
                D2D1_POINT_2F label_a=path.points[middle-1],label_b=path.points[middle];
                float label_x=(label_a.x+label_b.x)*.5f,label_y=(label_a.y+label_b.y)*.5f;
                D2D1_RECT_F label_rect;
                int attempt;
                tinta_utf8_to_utf16(edge->label,strlen(edge->label),&label);
                label_size=measure(app,app->ui_format,label.data,label.len);
                for (attempt = 0; attempt < 9; attempt++) {
                    int magnitude = (attempt + 1) / 2;
                    float shift = attempt == 0 ? 0 :
                        scale(app, 12) * magnitude * (attempt & 1 ? 1 : -1);
                    float shifted_x = label_x;
                    float shifted_y = label_y;
                    if (fabsf(label_b.x - label_a.x) >=
                        fabsf(label_b.y - label_a.y)) shifted_y += shift;
                    else shifted_x += shift;
                    label_rect = (D2D1_RECT_F){
                        shifted_x-label_size.cx*.5f-scale(app,4),
                        shifted_y-label_size.cy*.5f-scale(app,2),
                        shifted_x+label_size.cx*.5f+scale(app,4),
                        shifted_y+label_size.cy*.5f+scale(app,2)
                    };
                    if (!mermaid_label_collides(&graph, offset, top,
                                                &label_boxes, label_rect)) {
                        label_x = shifted_x;
                        label_y = shifted_y;
                        break;
                    }
                }
                if (!tinta_vec_push(&label_boxes, &label_rect)) {
                    tinta_str16_destroy(&label);
                    goto failed;
                }
                if (!add_rect(app,label_rect.left,label_rect.top,
                              label_rect.right,label_rect.bottom,
                              app->theme->background,scale(app,2),false,0) ||
                    !add_rect(app,label_rect.left,label_rect.top,
                              label_rect.right,label_rect.bottom,
                              app->theme->quote,scale(app,2),true,
                              scale(app,1))) {
                    tinta_str16_destroy(&label);
                    goto failed;
                }
                if (!append_run(app,label.data,label.len,app->ui_format,
                                app->theme->text,
                                label_x-label_size.cx*.5f,
                                label_y-label_size.cy*.5f,NULL,NULL)) {
                    tinta_str16_destroy(&label);
                    goto failed;
                }
                diagram_bottom=maxf(diagram_bottom,label_rect.bottom);
                tinta_str16_destroy(&label);
            }
        }
        for (i = 0; i < parsed.diagram.node_count; i++) {
            const TintaMermaidNode *node = &parsed.diagram.nodes[i]; TintaMermaidRect box = graph.nodes[i];
            const TintaMermaidStyle *class_style =
                tinta_mermaid_find_class_style(&parsed.diagram, node->class_name);
            uint32_t fill = node->style.has_fill ? node->style.fill.rgb :
                class_style && class_style->has_fill ? class_style->fill.rgb :
                app->theme->code_background;
            uint32_t stroke = node->style.has_stroke ? node->style.stroke.rgb :
                class_style && class_style->has_stroke ? class_style->stroke.rgb :
                app->theme->accent;
            uint32_t text = node->style.has_text ? node->style.text.rgb :
                class_style && class_style->has_text ? class_style->text.rgb :
                app->theme->text;
            float fill_opacity = node->style.has_fill ? node->style.fill.alpha :
                class_style && class_style->has_fill ? class_style->fill.alpha : 1.0f;
            float stroke_opacity = node->style.has_stroke ? node->style.stroke.alpha :
                class_style && class_style->has_stroke ? class_style->stroke.alpha : 1.0f;
            float text_opacity = node->style.has_text ? node->style.text.alpha :
                class_style && class_style->has_text ? class_style->text.alpha : 1.0f;
            float stroke_width = node->style.has_stroke_width ?
                node->style.stroke_width :
                class_style && class_style->has_stroke_width ?
                    class_style->stroke_width : 1.5f;
            TintaStr16 wide = {0};
            box.left += offset; box.right += offset; box.top += top; box.bottom += top;
            {
                TintaDrawShape shape = node->shape == TINTA_MERMAID_DIAMOND ? TINTA_DRAW_SHAPE_DIAMOND :
                            node->shape == TINTA_MERMAID_CIRCLE ? TINTA_DRAW_SHAPE_ELLIPSE :
                            node->shape == TINTA_MERMAID_HEXAGON ? TINTA_DRAW_SHAPE_HEXAGON :
                            node->shape == TINTA_MERMAID_STADIUM ? TINTA_DRAW_SHAPE_STADIUM :
                            node->shape == TINTA_MERMAID_RECTANGLE ? TINTA_DRAW_SHAPE_RECTANGLE :
                            TINTA_DRAW_SHAPE_ROUNDED;
                if (!add_shape(app,box.left,box.top,box.right,box.bottom,
                               fill,fill_opacity,shape,false,0) ||
                    !add_shape(app,box.left,box.top,box.right,box.bottom,
                               stroke,stroke_opacity,shape,true,
                               scale(app,stroke_width)))
                    goto failed;
            }
            tinta_utf8_to_utf16(node->label, strlen(node->label), &wide);
            {
                float label_width=maxf(1,box.right-box.left-scale(app,20));
                SIZE label_size=measure_wrapped(app,app->body_format,
                                                wide.data,wide.len,label_width);
                TintaTextRun *label_run = NULL;
                if (!append_run_width(app, wide.data, wide.len,
                                      app->body_format, text,
                                      box.left+scale(app,10),
                                      (box.top+box.bottom-label_size.cy)*.5f,
                                      label_width,&label_run)) {
                    tinta_str16_destroy(&wide);
                    goto failed;
                }
                label_run->opacity = text_opacity;
            }
            tinta_str16_destroy(&wide);
        }
        {
            TintaHorizontalRegion *region = horizontal_region(app, region_index);
            if (region) region->viewport.bottom = (LONG)diagram_bottom;
        }
        finish_horizontal_region(app, region_index);
        if (app->focus_mermaid_on_next_layout && graph.node_count) {
            bool *has_incoming = (bool *)calloc(
                parsed.diagram.node_count, sizeof(*has_incoming));
            size_t root_index = 0;
            if (has_incoming) {
                float root_center;
                TintaHorizontalRegion *region = horizontal_region(
                    app, region_index);
                for (i = 0; i < parsed.diagram.edge_count; i++) {
                    size_t target = parsed.diagram.edges[i].to;
                    if (target < parsed.diagram.node_count)
                        has_incoming[target] = true;
                }
                for (i = 0; i < parsed.diagram.node_count; i++) {
                    if (!has_incoming[i]) {
                        root_index = i;
                        break;
                    }
                }
                root_center = offset +
                    (graph.nodes[root_index].left +
                     graph.nodes[root_index].right) * 0.5f;
                if (region) {
                    float visible = (float)(region->viewport.right -
                                            region->viewport.left);
                    float maximum = maxf(0,
                        region->content_right - region->content_left - visible);
                    region->scroll_x = minf(maxf(
                        root_center - region->content_left - visible * 0.5f,
                        0), maximum);
                }
                free(has_incoming);
            }
            app->focus_mermaid_on_next_layout = false;
        }
        {
            const TintaHorizontalRegion *region = horizontal_region_const(
                app, region_index);
            if (mermaid_block_index < app->mermaid_blocks.len) {
                TintaMermaidBlock *block = TINTA_VEC_PTR(
                    TintaMermaidBlock, app->mermaid_blocks,
                    mermaid_block_index);
                block->rect.bottom = (LONG)(diagram_bottom +
                    (region && region->overflow ? ui_scale(app, 14) : 0));
            }
            *y = diagram_bottom + (region && region->overflow ?
                 ui_scale(app, 14) : 0) + scale(app, 24);
        }
    }
    tinta_mermaid_layout_destroy(&graph); tinta_str8_destroy(&source); free(sizes); tinta_vec_destroy(&label_boxes); return true;
failed:
    app->active_horizontal_region = SIZE_MAX;
    tinta_mermaid_layout_destroy(&graph); tinta_str8_destroy(&source); free(sizes); tinta_vec_destroy(&label_boxes); return false;
}
#endif

static bool add_scroll_anchor(TintaApp *app, const TintaElement *element,
                              float rendered_y) {
    TintaScrollAnchor anchor;
    TintaScrollAnchor *last;
    if (element->source_offset == SIZE_MAX) return true;
    if (app->scroll_anchors.len) {
        last = TINTA_VEC_PTR(TintaScrollAnchor, app->scroll_anchors,
                             app->scroll_anchors.len - 1);
        if (element->source_offset <= last->source_offset) return true;
    }
    anchor.source_offset = element->source_offset;
    anchor.rendered_y = rendered_y;
    return tinta_vec_push(&app->scroll_anchors, &anchor) != NULL;
}

static bool layout_element(TintaApp *app, const TintaElement *element,
                           float *y, float left, float right) {
    if (!add_scroll_anchor(app, element, *y)) return false;
    switch (element->type) {
        case TINTA_ELEMENT_DOCUMENT: return layout_children(app, element, y, left, right);
        case TINTA_ELEMENT_PARAGRAPH: return layout_paragraph(app, element, y, left, right);
        case TINTA_ELEMENT_HEADING: return layout_heading(app, element, y, left, right);
        case TINTA_ELEMENT_CODE_BLOCK:
#if TINTA_ENABLE_MERMAID
            if (element->language && !_stricmp(element->language, "mermaid"))
                return layout_mermaid(app, element, y, left, right);
#endif
            return layout_code(app, element, y, left, right);
        case TINTA_ELEMENT_MERMAID_DIAGRAM:
#if TINTA_ENABLE_MERMAID
            return layout_mermaid(app, element, y, left, right);
#else
            return layout_code(app, element, y, left, right);
#endif
        case TINTA_ELEMENT_BLOCK_QUOTE: {
            float top = *y;
            uint32_t bar_color = app->theme->quote;
            if(element->alert_kind) {
                static const wchar_t *names[] = {
                    L"", L"\x24d8  Note", L"\x2605  Tip",
                    L"\x2757  Important", L"\x26a0  Warning",
                    L"\x26d4  Caution"
                };
                static const uint32_t light_colors[] = {
                    0, 0x0969DA, 0x1A7F37, 0x8250DF, 0x9A6700, 0xCF222E
                };
                static const uint32_t dark_colors[] = {
                    0, 0x4493F8, 0x3FB950, 0xAB7DF8, 0xD29922, 0xF85149
                };
                TintaTextRun *label=NULL;
                int alert_kind = element->alert_kind;
                if (alert_kind < 1 || alert_kind > 5) alert_kind = 1;
                bar_color = app->theme->dark ? dark_colors[alert_kind] :
                                              light_colors[alert_kind];
                if (!append_run(app,names[alert_kind],
                                wcslen(names[alert_kind]),
                                app->bold_format,bar_color,
                                left+scale(app,18),*y,NULL,&label)) return false;
                *y += label ? label->height + scale(app,4) : scale(app,24);
            }
            if (!layout_children(app, element, y, left + scale(app, 18), right)) return false;
            return add_rect(app, left, top, left + scale(app, 4),
                            *y - scale(app, 6), bar_color, 0, false, 0);
        }
        case TINTA_ELEMENT_LIST: return layout_list(app, element, y, left, right);
        /* MD4C omits paragraph wrappers for tight list items, so treat the
           list item itself as an inline paragraph. */
        case TINTA_ELEMENT_LIST_ITEM: return layout_paragraph(app, element, y, left, right);
        case TINTA_ELEMENT_HORIZONTAL_RULE:
            *y += scale(app, 16);
            add_line(app, left, *y, right, *y, app->theme->quote, scale(app, 1));
            *y += scale(app, 16);
            return true;
        case TINTA_ELEMENT_TABLE: return layout_table(app, element, y, left, right);
        case TINTA_ELEMENT_IMAGE: return layout_image(app, element, y, left, right);
        default: return element->child_count ? layout_children(app, element, y, left, right) : true;
    }
}

static void validate_document_positions(TintaApp *app) {
    if (app->selection_anchor > app->doc_text.len)
        app->selection_anchor = app->doc_text.len;
    if (app->selection_focus > app->doc_text.len)
        app->selection_focus = app->doc_text.len;
}

bool tinta_layout_document(TintaApp *app) {
    uint64_t start = performance_time_us();
    float width = viewport_width(app);
    float left = scale(app, app->page_margin_left);
    float right = scale(app, app->page_margin_right);
    float y = scale(app, app->page_margin_top);
    if (!app->document.root || width <= 0) {
        app->layout_complete = true;
        app->layout_dirty = false;
        return true;
    }
    capture_scroll_anchor(app);
    tinta_layout_clear(app); app->content_width = width;
    if (!layout_element(app, app->document.root, &y, left,
                        maxf(left + 1, width - right))) return false;
    app->content_height = y + scale(app, app->page_margin_bottom);
    app->layout_time_us = (size_t)(performance_time_us() - start);
    app->layout_next_block = app->document.root->child_count;
    app->layout_cursor_y = y;
    app->layout_complete = true;
    app->layout_chunk_posted = false;
    app->layout_dirty = false;
    validate_document_positions(app);
    restore_scroll_anchor(app, true);
    return true;
}

static bool layout_incremental_chunk(TintaApp *app, DWORD time_budget_ms,
                                     float target_y) {
    const TintaElement *root = app->document.root;
    float width = viewport_width(app);
    float left = scale(app, app->page_margin_left);
    float right = scale(app, app->page_margin_right);
    uint64_t start = performance_time_us();
    bool laid_out_block = false;
    if (!root || width <= 0) {
        app->layout_complete = true;
        app->layout_dirty = false;
        return true;
    }
    while (app->layout_next_block < root->child_count) {
        const TintaElement *block = root->children[app->layout_next_block];
        if (!layout_element(app, block, &app->layout_cursor_y, left,
                            maxf(left + 1, width - right))) return false;
        app->layout_next_block++;
        laid_out_block = true;
        if (app->layout_cursor_y >= target_y) break;
        if (performance_time_us() - start >= (uint64_t)time_budget_ms * 1000)
            break;
    }
    app->layout_time_us += (size_t)(performance_time_us() - start);
    app->layout_complete = app->layout_next_block >= root->child_count;
    app->content_height = app->layout_cursor_y +
                          scale(app, app->page_margin_bottom);
    if (!laid_out_block && !root->child_count) app->layout_complete = true;
    if (app->layout_complete) validate_document_positions(app);
    restore_scroll_anchor(app, app->layout_complete);
    return true;
}

bool tinta_layout_document_viewport_first(TintaApp *app) {
    float width = viewport_width(app);
    float target_y = app->scroll_y + app->height * 2.0f;
    capture_scroll_anchor(app);
    tinta_layout_clear(app);
    app->content_width = width;
    app->content_height = 0;
    app->layout_time_us = 0;
    app->layout_next_block = 0;
    app->layout_cursor_y = scale(app, app->page_margin_top);
    app->layout_complete = false;
    app->layout_chunk_posted = false;
    app->layout_dirty = false;
    return layout_incremental_chunk(app, 10, target_y);
}

bool tinta_layout_continue(TintaApp *app) {
    if (app->layout_dirty || app->layout_complete) return true;
    return layout_incremental_chunk(app, 8, FLT_MAX);
}

static RECT code_button_rect(const TintaApp *app, const TintaCodeBlock *block) {
    LONG width = (LONG)ui_scale(app, 78);
    LONG height = (LONG)ui_scale(app, 28);
    LONG padding = (LONG)scale(app, 8);
    LONG visible_right = (LONG)(app->scroll_x + viewport_width(app));
    LONG header_height = (LONG)code_header_height(app);
    RECT result;
    result.right = min(block->rect.right, visible_right) - padding;
    result.left = result.right - width;
    result.top = block->rect.top + max(0L, (header_height - height) / 2);
    result.bottom = result.top + height;
    return result;
}

static RECT mermaid_button_rect(const TintaApp *app,
                                const TintaMermaidBlock *block) {
    LONG width = (LONG)ui_scale(app, 78);
    LONG height = (LONG)ui_scale(app, 28);
    LONG padding = (LONG)scale(app, 8);
    LONG header_height = (LONG)code_header_height(app);
    LONG visible_right = (LONG)(app->scroll_x + viewport_width(app));
    RECT result;
    result.right = min(block->rect.right, visible_right) - padding;
    result.left = result.right - width;
    result.top = block->rect.top + max(0L, (header_height - height) / 2);
    result.bottom = result.top + height;
    return result;
}

static RECT document_button_rect(const TintaApp *app) {
    LONG width = (LONG)ui_scale(app, 78);
    LONG height = (LONG)ui_scale(app, 28);
    LONG right_padding = (LONG)ui_scale(
        app, app->content_height > app->height ? 20.0f : 12.0f);
    LONG top_padding = (LONG)ui_scale(app, 12);
    RECT result;
    result.right = (LONG)(app->scroll_x + viewport_width(app)) - right_padding;
    result.left = result.right - width;
    result.top = top_padding;
    result.bottom = result.top + height;
    return result;
}

int tinta_code_block_at(const TintaApp *app, int x, int y) {
    size_t i;
    float document_x = x - viewport_x(app) + app->scroll_x;
    float document_y = y + app->scroll_y;
    for (i = 0; i < app->code_blocks.len; i++) {
        const TintaCodeBlock *block = TINTA_VEC_PTR(TintaCodeBlock, app->code_blocks, i);
        if (document_x >= block->rect.left && document_x <= block->rect.right &&
            document_y >= block->rect.top && document_y <= block->rect.bottom)
            return (int)i;
    }
    return -1;
}

bool tinta_code_button_at(const TintaApp *app, int x, int y) {
    int index = tinta_code_block_at(app, x, y);
    float document_x = x - viewport_x(app) + app->scroll_x;
    float document_y = y + app->scroll_y;
    RECT button;
    if (index < 0) return false;
    button = code_button_rect(app, TINTA_VEC_PTR(TintaCodeBlock, app->code_blocks, index));
    return document_x >= button.left && document_x <= button.right &&
           document_y >= button.top && document_y <= button.bottom;
}

int tinta_mermaid_block_at(const TintaApp *app, int x, int y) {
    size_t i;
    float document_x = x - viewport_x(app) + app->scroll_x;
    float document_y = y + app->scroll_y;
    for (i = 0; i < app->mermaid_blocks.len; i++) {
        const TintaMermaidBlock *block = TINTA_VEC_PTR(
            TintaMermaidBlock, app->mermaid_blocks, i);
        if (document_x >= block->rect.left &&
            document_x <= block->rect.right &&
            document_y >= block->rect.top &&
            document_y <= block->rect.bottom)
            return (int)i;
    }
    return -1;
}

bool tinta_mermaid_button_at(const TintaApp *app, int x, int y) {
    int index = tinta_mermaid_block_at(app, x, y);
    float document_x = x - viewport_x(app) + app->scroll_x;
    float document_y = y + app->scroll_y;
    RECT button;
    if (index < 0) return false;
    button = mermaid_button_rect(app, TINTA_VEC_PTR(
        TintaMermaidBlock, app->mermaid_blocks, index));
    return document_x >= button.left && document_x <= button.right &&
           document_y >= button.top && document_y <= button.bottom;
}

bool tinta_document_button_at(const TintaApp *app, int x, int y) {
    float document_x;
    float document_y;
    RECT button;
    if (!app || !app->document_copy_button_enabled) return false;
    document_x = x - viewport_x(app) + app->scroll_x;
    document_y = y + app->scroll_y;
    button = document_button_rect(app);
    return button.left >= app->scroll_x &&
           document_x >= button.left && document_x <= button.right &&
           document_y >= button.top && document_y <= button.bottom;
}

static D2D1_COLOR_F d2d_color(uint32_t color) {
    D2D1_COLOR_F result;
    result.r = ((color >> 16) & 0xff) / 255.0f;
    result.g = ((color >> 8) & 0xff) / 255.0f;
    result.b = (color & 0xff) / 255.0f;
    result.a = 1.0f;
    return result;
}

static void set_brush(TintaApp *app, uint32_t color) {
    D2D1_COLOR_F value = d2d_color(color);
    app->brush->lpVtbl->SetColor(app->brush, &value);
}

static void set_brush_alpha(TintaApp *app, uint32_t color, float opacity) {
    D2D1_COLOR_F value = d2d_color(color);
    value.a = opacity;
    app->brush->lpVtbl->SetColor(app->brush, &value);
}

static void set_brush_gray(TintaApp *app, float value, float opacity) {
    D2D1_COLOR_F color = {value, value, value, opacity};
    app->brush->lpVtbl->SetColor(app->brush, &color);
}

static D2D1_RECT_F d2d_rect(const RECT *rect) {
    D2D1_RECT_F result;
    result.left = (FLOAT)rect->left; result.top = (FLOAT)rect->top;
    result.right = (FLOAT)rect->right; result.bottom = (FLOAT)rect->bottom;
    return result;
}

static void fill_color(TintaApp *app, const RECT *rect, uint32_t color) {
    D2D1_RECT_F area = d2d_rect(rect);
    set_brush(app, color);
    app->render_target->lpVtbl->FillRectangle(
        app->render_target, &area, (ID2D1Brush *)app->brush);
}

static void draw_run(TintaApp *app, const TintaTextRun *run, float vx, float scroll) {
    D2D1_POINT_2F origin = {vx + run->x, run->y - scroll};
    D2D1_POINT_2F a, b;
    if (origin.y + run->height < 0 || origin.y > app->height) return;
    set_brush_alpha(app, run->color, run->opacity);
    tinta_draw_text_layout(app, origin, run->layout,
                           (ID2D1Brush *)app->brush,
                           D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    if (run->underline || run->strikethrough) {
        float y = origin.y + run->height * (run->strikethrough ? 0.55f : 0.92f);
        a = (D2D1_POINT_2F){origin.x, y};
        b = (D2D1_POINT_2F){origin.x + run->width, y};
        app->render_target->lpVtbl->DrawLine(
            app->render_target, a, b, (ID2D1Brush *)app->brush,
            maxf(1.0f, scale(app, 1.0f)), NULL);
    }
}

static void highlight_run(TintaApp *app, const TintaTextRun *run, float vx,
                          float scroll, size_t start, size_t end, uint32_t color) {
    size_t local_start, local_end;
    FLOAT x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    TintaDWriteHitTestMetrics metrics;
    RECT rect;
    if (run->y - scroll + run->height < 0 ||
        run->y - scroll > app->height ||
        end <= run->doc_start || start >= run->doc_start + run->doc_length)
        return;
    local_start = start > run->doc_start ? start - run->doc_start : 0;
    local_end = end < run->doc_start + run->doc_length ? end - run->doc_start : run->doc_length;
    if (local_start >= local_end || local_start > UINT32_MAX || local_end > UINT32_MAX) return;
    if (FAILED(run->layout->lpVtbl->HitTestTextPosition(
            run->layout, (UINT32)local_start, FALSE, &x1, &y1, &metrics))) return;
    if (FAILED(run->layout->lpVtbl->HitTestTextPosition(
            run->layout, (UINT32)(local_end - 1), TRUE, &x2, &y2, &metrics))) return;
    rect.left = (LONG)(vx + run->x + x1);
    rect.right = (LONG)ceilf(vx + run->x + x2);
    rect.top = (LONG)(run->y - scroll);
    rect.bottom = (LONG)ceilf(run->y - scroll + run->height);
    fill_color(app, &rect, color);
}

static void immediate_text(TintaApp *app, IDWriteTextFormat *format,
                           const wchar_t *text, RECT rect, uint32_t color) {
    IDWriteTextLayout *layout = NULL;
    TintaDWriteTextMetrics metrics;
    D2D1_POINT_2F origin = {(FLOAT)rect.left, (FLOAT)rect.top};
    float width = maxf(1, (float)(rect.right - rect.left));
    float height = maxf(1, (float)(rect.bottom - rect.top));
    HRESULT hr = app->dwrite_factory->lpVtbl->CreateTextLayout(
        app->dwrite_factory, text, (UINT32)wcslen(text), format, width, height,
        &layout);
    if (FAILED(hr) || !layout) return;
    apply_font_fallback(app, layout);
    memset(&metrics, 0, sizeof(metrics));
    layout->lpVtbl->GetMetrics(layout, &metrics);
    set_brush(app, color);
    tinta_draw_text_layout(app, origin, layout,
                           (ID2D1Brush *)app->brush,
                           D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    layout->lpVtbl->Release(layout);
}

static void immediate_text_ellipsis(TintaApp *app, IDWriteTextFormat *format,
                                    const wchar_t *text, RECT rect,
                                    uint32_t color, bool keep_end) {
    const wchar_t ellipsis[] = L"\x2026";
    size_t length = text ? wcslen(text) : 0;
    float available = maxf(1, (float)(rect.right - rect.left));
    SIZE full = measure(app, format, text ? text : L"", length);
    TintaStr16 clipped = {0};
    size_t low = 0;
    size_t high = length;
    if ((float)full.cx <= available || !length) {
        immediate_text(app, format, text ? text : L"", rect, color);
        return;
    }
    while (low < high) {
        size_t middle = low + (high - low + 1) / 2;
        SIZE part = {0};
        if (keep_end) {
            TintaStr16 candidate = {0};
            if (!tinta_str16_append(&candidate, ellipsis, 1) ||
                !tinta_str16_append(
                    &candidate, text + length - middle, middle))
                part.cx = INT_MAX;
            else
                part = measure(app, format, candidate.data, candidate.len);
            tinta_str16_destroy(&candidate);
        } else {
            TintaStr16 candidate = {0};
            if (!tinta_str16_append(&candidate, text, middle) ||
                !tinta_str16_append(&candidate, ellipsis, 1))
                part.cx = INT_MAX;
            else
                part = measure(app, format, candidate.data, candidate.len);
            tinta_str16_destroy(&candidate);
        }
        if ((float)part.cx <= available) low = middle;
        else high = middle - 1;
    }
    if (keep_end) {
        if (!tinta_str16_append(&clipped, ellipsis, 1) ||
            !tinta_str16_append(&clipped, text + length - low, low))
            tinta_str16_clear(&clipped);
    } else {
        if (!tinta_str16_append(&clipped, text, low) ||
            !tinta_str16_append(&clipped, ellipsis, 1))
            tinta_str16_clear(&clipped);
    }
    immediate_text(app, format, clipped.len ? clipped.data : ellipsis,
                   rect, color);
    tinta_str16_destroy(&clipped);
}

static bool draw_polygon(TintaApp *app, const D2D1_POINT_2F *points, UINT count,
                         bool outline, float stroke) {
    ID2D1PathGeometry *geometry = NULL;
    ID2D1GeometrySink *sink = NULL;
    HRESULT hr;
    if (count < 3) return false;
    hr = app->d2d_factory->lpVtbl->CreatePathGeometry(app->d2d_factory, &geometry);
    if (SUCCEEDED(hr)) hr = geometry->lpVtbl->Open(geometry, &sink);
    if (SUCCEEDED(hr)) {
        sink->lpVtbl->SetFillMode(sink, D2D1_FILL_MODE_WINDING);
        sink->lpVtbl->BeginFigure(sink, points[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->lpVtbl->AddLines(sink, points + 1, count - 1);
        sink->lpVtbl->EndFigure(sink, D2D1_FIGURE_END_CLOSED);
        hr = sink->lpVtbl->Close(sink);
    }
    if (SUCCEEDED(hr)) {
        if (outline)
            app->render_target->lpVtbl->DrawGeometry(
                app->render_target, (ID2D1Geometry *)geometry,
                (ID2D1Brush *)app->brush, stroke, NULL);
        else
            app->render_target->lpVtbl->FillGeometry(
                app->render_target, (ID2D1Geometry *)geometry,
                (ID2D1Brush *)app->brush, NULL);
    }
    if (sink) sink->lpVtbl->Release(sink);
    if (geometry) geometry->lpVtbl->Release(geometry);
    return SUCCEEDED(hr);
}

static void draw_rect_item(TintaApp *app, const TintaDrawRect *item,
                           float vx, float scroll) {
    D2D1_RECT_F rect = {(FLOAT)item->rect.left + vx,
                        (FLOAT)item->rect.top - scroll,
                        (FLOAT)item->rect.right + vx,
                        (FLOAT)item->rect.bottom - scroll};
    if (rect.bottom < 0 || rect.top > app->height) return;
    set_brush_alpha(app, item->color, item->opacity);
    if (item->shape == TINTA_DRAW_SHAPE_ELLIPSE) {
        D2D1_ELLIPSE ellipse = {{(rect.left + rect.right) * 0.5f,
                                 (rect.top + rect.bottom) * 0.5f},
                                (rect.right - rect.left) * 0.5f,
                                (rect.bottom - rect.top) * 0.5f};
        if (item->outline)
            app->render_target->lpVtbl->DrawEllipse(
                app->render_target, &ellipse, (ID2D1Brush *)app->brush,
                item->stroke, NULL);
        else
            app->render_target->lpVtbl->FillEllipse(
                app->render_target, &ellipse, (ID2D1Brush *)app->brush);
    } else if (item->shape == TINTA_DRAW_SHAPE_DIAMOND ||
               item->shape == TINTA_DRAW_SHAPE_HEXAGON) {
        D2D1_POINT_2F points[6];
        UINT count;
        float center_x = (rect.left + rect.right) * 0.5f;
        float center_y = (rect.top + rect.bottom) * 0.5f;
        if (item->shape == TINTA_DRAW_SHAPE_DIAMOND) {
            points[0] = (D2D1_POINT_2F){center_x, rect.top};
            points[1] = (D2D1_POINT_2F){rect.right, center_y};
            points[2] = (D2D1_POINT_2F){center_x, rect.bottom};
            points[3] = (D2D1_POINT_2F){rect.left, center_y};
            count = 4;
        } else {
            float inset = (rect.right - rect.left) * 0.25f;
            points[0] = (D2D1_POINT_2F){rect.left + inset, rect.top};
            points[1] = (D2D1_POINT_2F){rect.right - inset, rect.top};
            points[2] = (D2D1_POINT_2F){rect.right, center_y};
            points[3] = (D2D1_POINT_2F){rect.right - inset, rect.bottom};
            points[4] = (D2D1_POINT_2F){rect.left + inset, rect.bottom};
            points[5] = (D2D1_POINT_2F){rect.left, center_y};
            count = 6;
        }
        draw_polygon(app, points, count, item->outline, item->stroke);
    } else if (item->shape == TINTA_DRAW_SHAPE_RECTANGLE) {
        if (item->outline)
            app->render_target->lpVtbl->DrawRectangle(
                app->render_target, &rect, (ID2D1Brush *)app->brush,
                item->stroke, NULL);
        else
            app->render_target->lpVtbl->FillRectangle(
                app->render_target, &rect, (ID2D1Brush *)app->brush);
    } else {
        D2D1_ROUNDED_RECT rounded = {rect, item->radius, item->radius};
        if (item->outline)
            app->render_target->lpVtbl->DrawRoundedRectangle(
                app->render_target, &rounded, (ID2D1Brush *)app->brush,
                item->stroke, NULL);
        else
            app->render_target->lpVtbl->FillRoundedRectangle(
                app->render_target, &rounded, (ID2D1Brush *)app->brush);
    }
}

static void draw_line_item(TintaApp *app, const TintaDrawLine *item,
                           float vx, float scroll) {
    D2D1_POINT_2F a = {vx + item->a.x, item->a.y - scroll};
    D2D1_POINT_2F b = {vx + item->b.x, item->b.y - scroll};
    if (maxf(a.y, b.y) < 0 || minf(a.y, b.y) > app->height) return;
    set_brush_alpha(app, item->color, item->opacity);
    if (item->dashed) {
        float dx=b.x-a.x,dy=b.y-a.y,length=sqrtf(dx*dx+dy*dy);
        float dash=ui_scale(app,6),gap=ui_scale(app,4),position=0;
        if(length>0.001f) {
            dx/=length;dy/=length;
            while(position<length) {
                float end=minf(position+dash,length);
                D2D1_POINT_2F dash_a={a.x+dx*position,a.y+dy*position};
                D2D1_POINT_2F dash_b={a.x+dx*end,a.y+dy*end};
                app->render_target->lpVtbl->DrawLine(
                    app->render_target,dash_a,dash_b,
                    (ID2D1Brush *)app->brush,item->stroke,NULL);
                position+=dash+gap;
            }
        }
    } else {
        app->render_target->lpVtbl->DrawLine(
            app->render_target, a, b, (ID2D1Brush *)app->brush,
            item->stroke, NULL);
    }
}

static bool push_horizontal_region_clip(TintaApp *app, size_t index,
                                        float vx, float scroll) {
    const TintaHorizontalRegion *region = horizontal_region_const(app, index);
    D2D1_RECT_F clip;
    if (!region) return false;
    clip = (D2D1_RECT_F){vx + region->viewport.left,
                         region->viewport.top - scroll,
                         vx + region->viewport.right,
                         region->viewport.bottom - scroll};
    if (clip.bottom <= 0 || clip.top >= app->height ||
        clip.right <= 0 || clip.left >= app->width)
        return false;
    app->render_target->lpVtbl->PushAxisAlignedClip(
        app->render_target, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    return true;
}

static void draw_horizontal_region_scrollbar(TintaApp *app, size_t index) {
    TintaScrollbarGeometry geometry;
    D2D1_ROUNDED_RECT thumb;
    bool hovered;
    float height;
    float center;
    float gray;
    if (!horizontal_region_scrollbar_geometry(app, index, &geometry)) return;
    hovered = app->hovered_horizontal_region == (int)index ||
              app->dragging_horizontal_region == (int)index;
    height = ui_scale(app, hovered ? 8.0f : 5.0f);
    center = (geometry.top + geometry.bottom) * 0.5f;
    thumb = (D2D1_ROUNDED_RECT){{geometry.left, center - height * 0.5f,
                                 geometry.right, center + height * 0.5f},
                                height * 0.5f, height * 0.5f};
    gray = app->theme->dark ? 1.0f : 0.0f;
    set_brush_gray(app, gray, hovered ? 0.5f : 0.3f);
    app->render_target->lpVtbl->FillRoundedRectangle(
        app->render_target, &thumb, (ID2D1Brush *)app->brush);
}

static void highlight_run_with_region(TintaApp *app, const TintaTextRun *run,
                                      float vx, float scroll, size_t start,
                                      size_t end, uint32_t color) {
    const TintaHorizontalRegion *region = horizontal_region_const(
        app, run->horizontal_region);
    if (!region) {
        highlight_run(app, run, vx, scroll, start, end, color);
        return;
    }
    if (push_horizontal_region_clip(app, run->horizontal_region, vx, scroll)) {
        highlight_run(app, run, vx + horizontal_region_offset(region), scroll,
                      start, end, color);
        app->render_target->lpVtbl->PopAxisAlignedClip(app->render_target);
    }
}

static void draw_code_header(TintaApp *app, const TintaCodeBlock *block,
                             float vx, float scroll) {
    float document_left;
    float document_right;
    float padding = scale(app, 12);
    float top = block->rect.top - scroll;
    float bottom = top + code_header_height(app);
    RECT button;
    D2D1_RECT_F background;
    IDWriteTextLayout *label = NULL;
    if (!block->language || bottom < 0 || top > app->height) return;
    document_left = maxf((float)block->rect.left, app->scroll_x);
    document_right = minf((float)block->rect.right,
                          app->scroll_x + viewport_width(app));
    if (document_right <= document_left) return;
    background = (D2D1_RECT_F){document_left + vx, top,
                               document_right + vx, bottom};
    set_brush_alpha(app, app->theme->quote, app->theme->dark ? 0.16f : 0.10f);
    app->render_target->lpVtbl->FillRectangle(
        app->render_target, &background, (ID2D1Brush *)app->brush);
    set_brush_alpha(app, app->theme->quote, 0.45f);
    app->render_target->lpVtbl->DrawLine(
        app->render_target,
        (D2D1_POINT_2F){background.left, background.bottom},
        (D2D1_POINT_2F){background.right, background.bottom},
        (ID2D1Brush *)app->brush, maxf(1.0f, scale(app, 1)), NULL);
    button = code_button_rect(app, block);
    if (button.left > document_left + padding &&
        SUCCEEDED(app->dwrite_factory->lpVtbl->CreateTextLayout(
            app->dwrite_factory, block->language,
            (UINT32)wcslen(block->language), app->code_format,
            button.left - document_left - padding * 2,
            code_header_height(app), &label))) {
        D2D1_POINT_2F origin = {document_left + padding + vx, top};
        apply_font_fallback(app, label);
        label->lpVtbl->SetWordWrapping(
            label, TINTA_DWRITE_WORD_WRAPPING_NO_WRAP);
        label->lpVtbl->SetParagraphAlignment(
            label, TINTA_DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        set_brush_alpha(app, app->theme->syntax_comment, 0.95f);
        tinta_draw_text_layout(app, origin, label,
            (ID2D1Brush *)app->brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        label->lpVtbl->Release(label);
    }
}

static void draw_mermaid_header(TintaApp *app,
                                const TintaMermaidBlock *block,
                                float vx, float scroll) {
    static const wchar_t label_text[] = L"Mermaid";
    float document_left;
    float document_right;
    float padding = scale(app, 12);
    float top = block->rect.top - scroll;
    float bottom = top + code_header_height(app);
    RECT button;
    D2D1_RECT_F background;
    IDWriteTextLayout *label = NULL;
    if (bottom < 0 || top > app->height) return;
    document_left = maxf((float)block->rect.left, app->scroll_x);
    document_right = minf((float)block->rect.right,
                          app->scroll_x + viewport_width(app));
    if (document_right <= document_left) return;
    background = (D2D1_RECT_F){document_left + vx, top,
                               document_right + vx, bottom};
    set_brush_alpha(app, app->theme->quote,
                    app->theme->dark ? 0.16f : 0.10f);
    app->render_target->lpVtbl->FillRectangle(
        app->render_target, &background, (ID2D1Brush *)app->brush);
    set_brush_alpha(app, app->theme->quote, 0.45f);
    app->render_target->lpVtbl->DrawLine(
        app->render_target,
        (D2D1_POINT_2F){background.left, background.bottom},
        (D2D1_POINT_2F){background.right, background.bottom},
        (ID2D1Brush *)app->brush, maxf(1.0f, scale(app, 1)), NULL);
    button = mermaid_button_rect(app, block);
    if (button.left > document_left + padding &&
        SUCCEEDED(app->dwrite_factory->lpVtbl->CreateTextLayout(
            app->dwrite_factory, label_text, 7, app->code_format,
            button.left - document_left - padding * 2,
            code_header_height(app), &label))) {
        D2D1_POINT_2F origin = {document_left + padding + vx, top};
        apply_font_fallback(app, label);
        label->lpVtbl->SetWordWrapping(
            label, TINTA_DWRITE_WORD_WRAPPING_NO_WRAP);
        label->lpVtbl->SetParagraphAlignment(
            label, TINTA_DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        set_brush_alpha(app, app->theme->syntax_comment, 0.95f);
        tinta_draw_text_layout(app, origin, label,
            (ID2D1Brush *)app->brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        label->lpVtbl->Release(label);
    }
}

static void draw_copy_button(TintaApp *app, D2D1_RECT_F rect,
                             bool copied, bool hovered) {
    const wchar_t *button_text = copied ? L"Copied" : L"Copy";
    size_t button_text_length = copied ? 6 : 4;
    D2D1_ROUNDED_RECT button = {rect, ui_scale(app, 4), ui_scale(app, 4)};
    IDWriteTextLayout *label = NULL;
    float icon_size = ui_scale(app, 12);
    float icon_left = rect.left + ui_scale(app, 7);
    float icon_top = rect.top + (rect.bottom - rect.top - icon_size) * 0.5f;
    float text_left = icon_left + icon_size + ui_scale(app, 7);
    uint32_t foreground = copied ? app->theme->accent :
                                   app->theme->syntax_comment;
    if (hovered || copied) {
        set_brush_alpha(app, copied ? app->theme->accent : app->theme->quote,
                        copied ? 0.12f : 0.16f);
        app->render_target->lpVtbl->FillRoundedRectangle(
            app->render_target, &button, (ID2D1Brush *)app->brush);
    }
    set_brush_alpha(app, foreground, hovered || copied ? 1.0f : 0.82f);
    if (copied) {
        D2D1_POINT_2F a = {icon_left, icon_top + icon_size * 0.55f};
        D2D1_POINT_2F b = {icon_left + icon_size * 0.36f,
                           icon_top + icon_size * 0.88f};
        D2D1_POINT_2F c = {icon_left + icon_size,
                           icon_top + icon_size * 0.12f};
        app->render_target->lpVtbl->DrawLine(
            app->render_target, a, b, (ID2D1Brush *)app->brush,
            ui_scale(app, 1.5f), NULL);
        app->render_target->lpVtbl->DrawLine(
            app->render_target, b, c, (ID2D1Brush *)app->brush,
            ui_scale(app, 1.5f), NULL);
    } else {
        D2D1_RECT_F back = {icon_left + ui_scale(app, 3), icon_top,
            icon_left + icon_size, icon_top + icon_size - ui_scale(app, 3)};
        D2D1_RECT_F front = {icon_left, icon_top + ui_scale(app, 3),
            icon_left + icon_size - ui_scale(app, 3), icon_top + icon_size};
        app->render_target->lpVtbl->DrawRectangle(
            app->render_target, &back, (ID2D1Brush *)app->brush,
            ui_scale(app, 1.1f), NULL);
        app->render_target->lpVtbl->DrawRectangle(
            app->render_target, &front, (ID2D1Brush *)app->brush,
            ui_scale(app, 1.1f), NULL);
    }
    if (SUCCEEDED(app->dwrite_factory->lpVtbl->CreateTextLayout(
            app->dwrite_factory, button_text, (UINT32)button_text_length,
            app->chrome_format, rect.right - text_left - ui_scale(app, 4),
            rect.bottom - rect.top, &label))) {
        D2D1_POINT_2F origin = {text_left, rect.top};
        apply_font_fallback(app, label);
        label->lpVtbl->SetParagraphAlignment(
            label, TINTA_DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        set_brush_alpha(app, foreground, hovered || copied ? 1.0f : 0.82f);
        tinta_draw_text_layout(app, origin, label,
            (ID2D1Brush *)app->brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        label->lpVtbl->Release(label);
    }
}


void tinta_render(TintaApp *app) {
    size_t i;
    float vx;
    float scroll;
    HRESULT hr;
    if (app->layout_dirty) {
        bool laid_out = tinta_layout_document_viewport_first(app);
        if (!laid_out) return;
    }
    if (!app->layout_complete && !app->layout_chunk_posted) {
        app->layout_chunk_posted =
            PostMessageW(app->hwnd, TINTA_WM_LAYOUT_CHUNK, 0, 0) != FALSE;
    }
    if (app->layout_complete || !app->scroll_anchor_pending)
        app->scroll_y = minf(maxf(app->scroll_y, 0),
                             maxf(0, app->content_height - app->height));
    app->scroll_x = minf(maxf(app->scroll_x, 0),
                         maxf(0, app->content_width - viewport_width(app)));
    vx = viewport_x(app) - app->scroll_x;
    scroll = app->scroll_y;
    if (!app->render_target && !tinta_app_create_device(app)) return;
    app->render_target->lpVtbl->BeginDraw(app->render_target);
    app->draw_calls = 0;
    { D2D1_COLOR_F background = d2d_color(app->theme->background);
      app->render_target->lpVtbl->Clear(app->render_target, &background); }
    {
        D2D1_RECT_F clip = {viewport_x(app), 0, (FLOAT)app->width, (FLOAT)app->height};
        app->render_target->lpVtbl->PushAxisAlignedClip(
            app->render_target, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
        for (i = 0; i < app->rects.len; i++) {
            TintaDrawRect *item = TINTA_VEC_PTR(TintaDrawRect, app->rects, i);
            if (item->horizontal_region == SIZE_MAX)
                draw_rect_item(app, item, vx, scroll);
        }
        for (i = 0; i < app->lines.len; i++) {
            TintaDrawLine *item = TINTA_VEC_PTR(TintaDrawLine, app->lines, i);
            if (item->horizontal_region == SIZE_MAX)
                draw_line_item(app, item, vx, scroll);
        }
        {
            size_t region_index;
            for (region_index = 0;
                 region_index < app->horizontal_regions.len; region_index++) {
                const TintaHorizontalRegion *region = horizontal_region_const(
                    app, region_index);
                if (!push_horizontal_region_clip(
                        app, region_index, vx, scroll)) continue;
                for (i = 0; i < app->rects.len; i++) {
                    TintaDrawRect *item = TINTA_VEC_PTR(
                        TintaDrawRect, app->rects, i);
                    if (item->horizontal_region == region_index)
                        draw_rect_item(app, item,
                            vx + horizontal_region_offset(region), scroll);
                }
                for (i = 0; i < app->lines.len; i++) {
                    TintaDrawLine *item = TINTA_VEC_PTR(
                        TintaDrawLine, app->lines, i);
                    if (item->horizontal_region == region_index)
                        draw_line_item(app, item,
                            vx + horizontal_region_offset(region), scroll);
                }
                app->render_target->lpVtbl->PopAxisAlignedClip(
                    app->render_target);
            }
        }
#if TINTA_ENABLE_IMAGES
        for (i = 0; i < app->bitmaps.len; i++) {
            TintaDrawBitmap *item = TINTA_VEC_PTR(TintaDrawBitmap, app->bitmaps, i);
            TintaImageResource *resource;
            D2D1_RECT_F destination = {vx + item->rect.left,
                                       item->rect.top - scroll,
                                       vx + item->rect.right,
                                       item->rect.bottom - scroll};
            if (destination.bottom < 0 || destination.top > app->height)
                continue;
            if (item->resource_index >= app->image_resources.len) continue;
            resource = TINTA_VEC_PTR(TintaImageResource,
                app->image_resources, item->resource_index);
            if (!resource->bitmap && resource->source)
                app->render_target->lpVtbl->CreateBitmapFromWicBitmap(
                    app->render_target, resource->source, NULL,
                    &resource->bitmap);
            if (resource->bitmap)
                app->render_target->lpVtbl->DrawBitmap(
                    app->render_target, resource->bitmap, &destination, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
        }
#endif
        for (i = 0; i < app->code_blocks.len; i++)
            draw_code_header(app,
                TINTA_VEC_PTR(TintaCodeBlock, app->code_blocks, i), vx, scroll);
        for (i = 0; i < app->mermaid_blocks.len; i++)
            draw_mermaid_header(app,
                TINTA_VEC_PTR(TintaMermaidBlock, app->mermaid_blocks, i),
                vx, scroll);
        if (app->search_query.len && app->viewer_search_matches.len) {
            size_t match_index = 0;
            size_t run_index = 0;
            while (match_index < app->viewer_search_matches.len &&
                   run_index < app->text_runs.len) {
                TintaSearchMatch match = TINTA_VEC_AT(TintaSearchMatch,
                    app->viewer_search_matches, match_index);
                TintaTextRun *run = TINTA_VEC_PTR(
                    TintaTextRun, app->text_runs, run_index);
                size_t match_end = match.start + match.length;
                size_t run_end = run->doc_start + run->doc_length;
                uint32_t color = match_index == (size_t)app->viewer_search_index ?
                    0xF59E0B : 0xFACC15;
                if (match_end <= run->doc_start) {
                    match_index++;
                } else if (run_end <= match.start) {
                    run_index++;
                } else {
                    highlight_run_with_region(app, run, vx, scroll,
                                              match.start, match_end, color);
                    if (run_end < match_end) run_index++;
                    else match_index++;
                }
            }
        } else if (app->selection_anchor != app->selection_focus) {
            size_t start = min(app->selection_anchor, app->selection_focus);
            size_t end = max(app->selection_anchor, app->selection_focus);
            for (i = 0; i < app->text_runs.len; i++)
                highlight_run_with_region(app,
                    TINTA_VEC_PTR(TintaTextRun, app->text_runs, i),
                    vx, scroll, start, end, app->theme->quote);
        }
        for (i = 0; i < app->text_runs.len; i++) {
            TintaTextRun *run = TINTA_VEC_PTR(
                TintaTextRun, app->text_runs, i);
            if (run->horizontal_region == SIZE_MAX) {
                draw_run(app, run, vx, scroll);
                app->draw_calls++;
            }
        }
        {
            size_t region_index;
            for (region_index = 0;
                 region_index < app->horizontal_regions.len; region_index++) {
                const TintaHorizontalRegion *region = horizontal_region_const(
                    app, region_index);
                if (!push_horizontal_region_clip(
                        app, region_index, vx, scroll)) continue;
                for (i = 0; i < app->text_runs.len; i++) {
                    TintaTextRun *run = TINTA_VEC_PTR(
                        TintaTextRun, app->text_runs, i);
                    if (run->horizontal_region == region_index) {
                        draw_run(app, run,
                            vx + horizontal_region_offset(region), scroll);
                        app->draw_calls++;
                    }
                }
                app->render_target->lpVtbl->PopAxisAlignedClip(
                    app->render_target);
            }
        }
        if (app->hovered_code_block >= 0 &&
            (size_t)app->hovered_code_block < app->code_blocks.len) {
            TintaCodeBlock *block = TINTA_VEC_PTR(
                TintaCodeBlock, app->code_blocks, (size_t)app->hovered_code_block);
            bool copied = app->notice_kind == TINTA_NOTICE_COPIED &&
                          app->notice_code_block == app->hovered_code_block;
            bool over_button = tinta_code_button_at(
                app, app->mouse_x, app->mouse_y);
            RECT document_button = code_button_rect(app, block);
            D2D1_RECT_F button = {(float)document_button.left + vx,
                                  (float)document_button.top - scroll,
                                  (float)document_button.right + vx,
                                  (float)document_button.bottom - scroll};
            if (button.bottom > 0 && button.top < app->height)
                draw_copy_button(app, button, copied, over_button);
        }
        if (app->hovered_mermaid_block >= 0 &&
            (size_t)app->hovered_mermaid_block < app->mermaid_blocks.len) {
            TintaMermaidBlock *block = TINTA_VEC_PTR(
                TintaMermaidBlock, app->mermaid_blocks,
                (size_t)app->hovered_mermaid_block);
            bool copied = app->notice_kind == TINTA_NOTICE_COPIED &&
                          app->notice_mermaid_block ==
                          app->hovered_mermaid_block;
            bool over_button = tinta_mermaid_button_at(
                app, app->mouse_x, app->mouse_y);
            RECT document_button = mermaid_button_rect(app, block);
            D2D1_RECT_F button = {(float)document_button.left + vx,
                                  (float)document_button.top - scroll,
                                  (float)document_button.right + vx,
                                  (float)document_button.bottom - scroll};
            if (button.bottom > 0 && button.top < app->height)
                draw_copy_button(app, button, copied, over_button);
        }
        app->render_target->lpVtbl->PopAxisAlignedClip(app->render_target);
        for (i = 0; i < app->horizontal_regions.len; i++)
            draw_horizontal_region_scrollbar(app, i);
        if (app->document_copy_button_enabled) {
            RECT document_button = document_button_rect(app);
            D2D1_RECT_F button = {
                (float)document_button.left + vx,
                (float)document_button.top - scroll,
                (float)document_button.right + vx,
                (float)document_button.bottom - scroll};
            bool copied = app->notice_kind == TINTA_NOTICE_COPIED &&
                          app->notice_code_block < 0 &&
                          app->notice_mermaid_block < 0;
            if (document_button.left >= app->scroll_x &&
                button.bottom > 0 && button.top < app->height)
                draw_copy_button(app, button, copied,
                    tinta_document_button_at(
                        app, app->mouse_x, app->mouse_y));
        }
        {
            float visible = viewport_width(app);
            bool vertical = app->content_height > app->height;
            bool horizontal = app->content_width > visible;
            float gray = app->theme->dark ? 1.0f : 0.0f;
            if (vertical) {
                TintaScrollbarGeometry geometry;
                if (tinta_vertical_scrollbar_geometry(app, &geometry)) {
                    bool hovered = app->scrollbar_hovered || app->scrollbar_dragging;
                    float width = ui_scale(app, hovered ? 10.0f : 6.0f);
                    D2D1_ROUNDED_RECT thumb = {{app->width - width - ui_scale(app, 4), geometry.top,
                                                 app->width - ui_scale(app, 4), geometry.bottom},
                                                ui_scale(app, 3), ui_scale(app, 3)};
                    set_brush_gray(app, gray, hovered ? 0.5f : 0.3f);
                    app->render_target->lpVtbl->FillRoundedRectangle(
                        app->render_target, &thumb, (ID2D1Brush *)app->brush);
                }
            }
            if (horizontal) {
                TintaScrollbarGeometry geometry;
                if (tinta_horizontal_scrollbar_geometry(app, &geometry)) {
                    bool hovered = app->h_scrollbar_hovered || app->h_scrollbar_dragging;
                    float height = ui_scale(app, hovered ? 10.0f : 6.0f);
                    D2D1_ROUNDED_RECT thumb = {{geometry.left,
                                                 app->height - height - ui_scale(app, 4),
                                                 geometry.right,
                                                 app->height - ui_scale(app, 4)},
                                                ui_scale(app, 3), ui_scale(app, 3)};
                    set_brush_gray(app, gray, hovered ? 0.5f : 0.3f);
                    app->render_target->lpVtbl->FillRoundedRectangle(
                        app->render_target, &thumb, (ID2D1Brush *)app->brush);
                }
            }
        }
    }
    hr = app->render_target->lpVtbl->EndDraw(app->render_target, NULL, NULL);
    if (hr == D2DERR_RECREATE_TARGET) {
        tinta_app_discard_device(app);
        InvalidateRect(app->hwnd, NULL, FALSE);
    }
}

void tinta_scroll(TintaApp *app, float amount) {
    set_scroll_y(app, app->scroll_y + amount);
}

enum { TINTA_HIT_BUCKET_HEIGHT = 128 };

typedef struct HitCandidate {
    TintaTextRun *nearest;
    float nearest_y_distance;
    float nearest_x_distance;
} HitCandidate;

static int compare_hit_entries(const void *left, const void *right) {
    const TintaHitEntry *a = (const TintaHitEntry *)left;
    const TintaHitEntry *b = (const TintaHitEntry *)right;
    if (a->bucket != b->bucket) return a->bucket < b->bucket ? -1 : 1;
    if (a->run_index == b->run_index) return 0;
    return a->run_index < b->run_index ? -1 : 1;
}

static bool rebuild_hit_index(TintaApp *app) {
    size_t run_index;
    tinta_vec_clear(&app->hit_entries);
    for (run_index = 0; run_index < app->text_runs.len; run_index++) {
        const TintaTextRun *run =
            TINTA_VEC_PTR(TintaTextRun, app->text_runs, run_index);
        int first_bucket;
        int last_bucket;
        int bucket;
        if (!isfinite(run->y) || !isfinite(run->height) || run->height < 0 ||
            run->y < (float)INT_MIN * TINTA_HIT_BUCKET_HEIGHT ||
            run->y > (float)INT_MAX * TINTA_HIT_BUCKET_HEIGHT)
            continue;
        first_bucket = (int)floorf(run->y / TINTA_HIT_BUCKET_HEIGHT);
        last_bucket = (int)floorf(
            (run->y + maxf(run->height, 1.0f)) /
            TINTA_HIT_BUCKET_HEIGHT);
        if (last_bucket < first_bucket) last_bucket = first_bucket;
        for (bucket = first_bucket; bucket <= last_bucket; bucket++) {
            TintaHitEntry entry = {bucket, run_index};
            if (!tinta_vec_push(&app->hit_entries, &entry)) {
                tinta_vec_clear(&app->hit_entries);
                app->hit_index_dirty = false;
                return false;
            }
            if (bucket == INT_MAX) break;
        }
    }
    if (app->hit_entries.len > 1) {
        qsort(app->hit_entries.data, app->hit_entries.len,
              sizeof(TintaHitEntry), compare_hit_entries);
    }
    app->hit_index_dirty = false;
    return true;
}

static size_t hit_bucket_lower_bound(const TintaVec *entries, int bucket) {
    size_t low = 0;
    size_t high = entries->len;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        const TintaHitEntry *entry =
            TINTA_VEC_PTR(TintaHitEntry, *entries, middle);
        if (entry->bucket < bucket) low = middle + 1;
        else high = middle;
    }
    return low;
}

static bool consider_hit_run(TintaApp *app, TintaTextRun *run, float x, float y,
                             size_t *position,
                             const char **url, HitCandidate *candidate) {
    const TintaHorizontalRegion *region = horizontal_region_const(
        app, run->horizontal_region);
    if (region) {
        if (x < region->viewport.left || x > region->viewport.right ||
            y < region->viewport.top || y > region->viewport.bottom)
            return false;
        x -= horizontal_region_offset(region);
    }
    float right = run->x + run->width;
    float bottom = run->y + run->height;
    float y_distance = y < run->y ? run->y - y :
        (y > bottom ? y - bottom : 0);
    float x_distance = x < run->x ? run->x - x :
        (x > right ? x - right : 0);
    if (x >= run->x && x <= right && y >= run->y && y <= bottom) {
        BOOL trailing = FALSE;
        BOOL inside = FALSE;
        TintaDWriteHitTestMetrics metrics;
        if (position && SUCCEEDED(run->layout->lpVtbl->HitTestPoint(
                run->layout, x - run->x, y - run->y,
                &trailing, &inside, &metrics))) {
            size_t offset = metrics.textPosition +
                (trailing ? metrics.length : 0);
            *position = run->doc_start + min(offset, run->text_length);
        }
        if (url && run->url[0]) *url = run->url;
        return true;
    }
    if (y_distance < candidate->nearest_y_distance ||
        (fabsf(y_distance - candidate->nearest_y_distance) < 0.01f &&
         x_distance < candidate->nearest_x_distance)) {
        candidate->nearest = run;
        candidate->nearest_y_distance = y_distance;
        candidate->nearest_x_distance = x_distance;
    }
    return false;
}

static bool scan_hit_bucket(TintaApp *app, int bucket, float x, float y,
                            size_t *position, const char **url,
                            HitCandidate *candidate) {
    size_t index = hit_bucket_lower_bound(&app->hit_entries, bucket);
    while (index < app->hit_entries.len) {
        const TintaHitEntry *entry =
            TINTA_VEC_PTR(TintaHitEntry, app->hit_entries, index);
        if (entry->bucket != bucket) break;
        if (entry->run_index < app->text_runs.len &&
            consider_hit_run(app,
                TINTA_VEC_PTR(TintaTextRun, app->text_runs, entry->run_index),
                x, y, position, url, candidate))
            return true;
        index++;
    }
    return false;
}

static bool hit_test_indexed(TintaApp *app, float x, float y,
                             size_t *position, const char **url,
                             HitCandidate *candidate) {
    int bucket;
    int minimum_bucket;
    int maximum_bucket;
    int radius;
    if (!app->hit_entries.len) return false;
    bucket = (int)floorf(y / TINTA_HIT_BUCKET_HEIGHT);
    minimum_bucket = TINTA_VEC_AT(TintaHitEntry, app->hit_entries, 0).bucket;
    maximum_bucket = TINTA_VEC_AT(
        TintaHitEntry, app->hit_entries, app->hit_entries.len - 1).bucket;
    if (scan_hit_bucket(app, bucket, x, y, position, url, candidate))
        return true;
    for (radius = 1; bucket - radius >= minimum_bucket ||
                     bucket + radius <= maximum_bucket; radius++) {
        if (bucket - radius >= minimum_bucket &&
            scan_hit_bucket(app, bucket - radius, x, y,
                            position, url, candidate))
            return true;
        if (bucket + radius <= maximum_bucket &&
            scan_hit_bucket(app, bucket + radius, x, y,
                            position, url, candidate))
            return true;
        if (candidate->nearest) {
            float above = bucket - radius > minimum_bucket ?
                y - (bucket - radius) * TINTA_HIT_BUCKET_HEIGHT : FLT_MAX;
            float below = bucket + radius < maximum_bucket ?
                (bucket + radius + 1) * TINTA_HIT_BUCKET_HEIGHT - y : FLT_MAX;
            if (minf(above, below) > candidate->nearest_y_distance)
                break;
        }
    }
    return false;
}

static bool hit_test_linear(TintaApp *app, float x, float y,
                            size_t *position, const char **url,
                            HitCandidate *candidate) {
    size_t index;
    for (index = 0; index < app->text_runs.len; index++) {
        if (consider_hit_run(app,
                TINTA_VEC_PTR(TintaTextRun, app->text_runs, index),
                x, y, position, url, candidate))
            return true;
    }
    return false;
}

void tinta_hit_test(TintaApp *app, float x, float y, size_t *position,
                    const char **url) {
    HitCandidate candidate = {0};
    bool exact;
    candidate.nearest_y_distance = FLT_MAX;
    candidate.nearest_x_distance = FLT_MAX;
    x -= viewport_x(app);
    x += app->scroll_x;
    y += app->scroll_y;
    if (position) *position = app->doc_text.len;
    if (url) *url = NULL;
    if (app->hit_index_dirty) rebuild_hit_index(app);
    if (app->hit_entries.len)
        exact = hit_test_indexed(app, x, y, position, url, &candidate);
    else
        exact = hit_test_linear(app, x, y, position, url, &candidate);
    if (!exact && position && candidate.nearest) {
        TintaTextRun *nearest = candidate.nearest;
        const TintaHorizontalRegion *region = horizontal_region_const(
            app, nearest->horizontal_region);
        float right = nearest->x + nearest->width;
        if (region) x -= horizontal_region_offset(region);
        if (x <= nearest->x) {
            *position = nearest->doc_start;
        } else if (x >= right) {
            *position = nearest->doc_start + nearest->doc_length;
        } else {
            BOOL trailing = FALSE, inside = FALSE;
            TintaDWriteHitTestMetrics metrics;
            float local_y = minf(maxf(y - nearest->y, 0), nearest->height);
            if (SUCCEEDED(nearest->layout->lpVtbl->HitTestPoint(
                    nearest->layout, x - nearest->x, local_y,
                    &trailing, &inside, &metrics))) {
                size_t offset = metrics.textPosition +
                    (trailing ? metrics.length : 0);
                *position = nearest->doc_start +
                    min(offset, nearest->text_length);
            }
        }
    }
}

bool tinta_text_at(TintaApp *app, float x, float y, const char **url) {
    HitCandidate candidate = {0};
    candidate.nearest_y_distance = FLT_MAX;
    candidate.nearest_x_distance = FLT_MAX;
    x -= viewport_x(app);
    x += app->scroll_x;
    y += app->scroll_y;
    if (url) *url = NULL;
    if (app->hit_index_dirty) rebuild_hit_index(app);
    if (app->hit_entries.len &&
        y >= (float)INT_MIN * TINTA_HIT_BUCKET_HEIGHT &&
        y <= (float)INT_MAX * TINTA_HIT_BUCKET_HEIGHT) {
        int bucket = (int)floorf(y / TINTA_HIT_BUCKET_HEIGHT);
        return scan_hit_bucket(app, bucket, x, y, NULL, url, &candidate);
    }
    return hit_test_linear(app, x, y, NULL, url, &candidate);
}

static void show_copied_notice(TintaApp *app, int code_block,
                               int mermaid_block) {
    app->notice_kind = TINTA_NOTICE_COPIED;
    app->notice_code_block = code_block;
    app->notice_mermaid_block = mermaid_block;
    if (!SetTimer(app->hwnd, TINTA_TIMER_NOTIFICATION, 1000, NULL)) {
        app->notice_kind = TINTA_NOTICE_NONE;
        app->notice_code_block = -1;
        app->notice_mermaid_block = -1;
    }
    InvalidateRect(app->hwnd, NULL, FALSE);
}

bool tinta_copy_selection(TintaApp *app) {
    size_t start;
    size_t end;
    if (!app) return false;
    start = min(app->selection_anchor, app->selection_focus);
    end = max(app->selection_anchor, app->selection_focus);
    if (start > app->doc_text.len) start = app->doc_text.len;
    if (end > app->doc_text.len) end = app->doc_text.len;
    if (start == end) {
        start = 0;
        end = app->doc_text.len;
    }
    if (end <= start) return false;
    return tinta_set_clipboard_text(
        app->hwnd, app->doc_text.data + start, end - start);
}

bool tinta_copy_code_at(TintaApp *app, int x, int y, bool *copied) {
    int index = tinta_code_block_at(app, x, y);
    TintaCodeBlock *block;
    bool success;
    if (copied) *copied = false;
    if (index < 0 || !tinta_code_button_at(app, x, y)) return false;
    block = TINTA_VEC_PTR(TintaCodeBlock, app->code_blocks, (size_t)index);
    success = tinta_set_clipboard_text(
        app->hwnd, block->text, wcslen(block->text));
    if (success) show_copied_notice(app, index, -1);
    if (copied) *copied = success;
    return true;
}

bool tinta_copy_mermaid_at(TintaApp *app, int x, int y, bool *copied) {
    int index = tinta_mermaid_block_at(app, x, y);
    TintaMermaidBlock *block;
    bool success;
    if (copied) *copied = false;
    if (index < 0 || !tinta_mermaid_button_at(app, x, y)) return false;
    block = TINTA_VEC_PTR(
        TintaMermaidBlock, app->mermaid_blocks, (size_t)index);
    success = tinta_set_clipboard_text(
        app->hwnd, block->text, wcslen(block->text));
    if (success) show_copied_notice(app, -1, index);
    if (copied) *copied = success;
    return true;
}

bool tinta_copy_document_at(TintaApp *app, int x, int y, bool *copied) {
    TintaStr16 wide = {0};
    bool success = false;
    if (copied) *copied = false;
    if (!tinta_document_button_at(app, x, y)) return false;
    if (tinta_utf8_to_utf16(app->source.data ? app->source.data : "",
                            app->source.len, &wide)) {
        success = tinta_set_clipboard_text(
            app->hwnd, wide.data ? wide.data : L"", wide.len);
    }
    tinta_str16_destroy(&wide);
    if (success) show_copied_notice(app, -1, -1);
    if (copied) *copied = success;
    return true;
}

bool tinta_jump_to_internal_link(TintaApp *app, const char *url) {
    TintaStr8 decoded = {0};
    TintaStr16 wide = {0};
    size_t i;
    bool found = false;
    if (!url || url[0] != '#') return false;
    for (i = 1; url[i]; i++) {
        unsigned char value;
        if (url[i] == '%' && url[i + 1] && url[i + 2] &&
            isxdigit((unsigned char)url[i + 1]) &&
            isxdigit((unsigned char)url[i + 2])) {
            char hex[3] = {url[i + 1], url[i + 2], 0};
            value = (unsigned char)strtoul(hex, NULL, 16);
            i += 2;
        } else value = (unsigned char)url[i];
        if (!tinta_str8_append_char(&decoded, (char)value)) goto done;
    }
    if (!tinta_utf8_to_utf16(decoded.data ? decoded.data : "", decoded.len, &wide))
        goto done;
    if (!wide.data) goto done;
    for (i = 0; i < app->headings.len; i++) {
        TintaHeading *heading = TINTA_VEC_PTR(TintaHeading, app->headings, i);
        if (heading->slug && !_wcsicmp(heading->slug, wide.data)) {
            app->scroll_y = maxf(0.0f, heading->y - ui_scale(app, 20));
            InvalidateRect(app->hwnd, NULL, FALSE);
            found = true;
            break;
        }
    }
done:
    tinta_str8_destroy(&decoded);
    tinta_str16_destroy(&wide);
    return found;
}
