#include "editor.h"

#include <imm.h>
#include <windowsx.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

typedef struct TintaEditorScrollbar {
    D2D1_RECT_F track;
    D2D1_RECT_F thumb;
    float maximum;
    float travel;
    bool visible;
} TintaEditorScrollbar;

static float editor_scale(const TintaEditor *editor, float value) {
    return value * editor->dpi_scale;
}

static float clamp_float(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static D2D1_COLOR_F editor_color(uint32_t color, float alpha) {
    D2D1_COLOR_F result;
    result.r = ((color >> 16) & 255) / 255.0f;
    result.g = ((color >> 8) & 255) / 255.0f;
    result.b = (color & 255) / 255.0f;
    result.a = alpha;
    return result;
}

static uint32_t editor_colorref(COLORREF color) {
    return ((uint32_t)GetRValue(color) << 16) |
           ((uint32_t)GetGValue(color) << 8) |
           GetBValue(color);
}

static void editor_set_brush(TintaEditor *editor,
                             uint32_t color, float alpha) {
    D2D1_COLOR_F value = editor_color(color, alpha);
    editor->brush->lpVtbl->SetColor(editor->brush, &value);
}

static void editor_release_unknown(void **object) {
    if (*object) {
        ((IUnknown *)*object)->lpVtbl->Release((IUnknown *)*object);
        *object = NULL;
    }
}

static void editor_notify(TintaEditor *editor, UINT code) {
    HWND parent = GetParent(editor->hwnd);
    if (parent)
        SendMessageW(parent, WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(editor->hwnd), code),
                     (LPARAM)editor->hwnd);
    if (code == EN_HSCROLL || code == EN_VSCROLL)
        tinta_editor_uia_raise_scroll_changed(editor);
}

static bool editor_wrap_enabled(const TintaEditor *editor) {
    if (editor->wrap_mode == TINTA_EDITOR_WRAP_ON) return true;
    if (editor->wrap_mode == TINTA_EDITOR_WRAP_OFF) return false;
    return !editor->style_no_wrap;
}

static size_t editor_length(const TintaEditor *editor) {
    return tinta_editor_document_length(&editor->document);
}

static bool is_high_surrogate(wchar_t character) {
    return character >= 0xd800 && character <= 0xdbff;
}

static bool is_low_surrogate(wchar_t character) {
    return character >= 0xdc00 && character <= 0xdfff;
}

static size_t editor_previous_position(const TintaEditor *editor,
                                       size_t position) {
    if (!position) return 0;
    position--;
    if (position && is_low_surrogate(
            tinta_editor_document_char_at(&editor->document, position)) &&
        is_high_surrogate(tinta_editor_document_char_at(
            &editor->document, position - 1)))
        position--;
    return position;
}

static size_t editor_next_position(const TintaEditor *editor,
                                   size_t position) {
    size_t length = editor_length(editor);
    if (position >= length) return length;
    if (is_high_surrogate(tinta_editor_document_char_at(
            &editor->document, position)) && position + 1 < length &&
        is_low_surrogate(tinta_editor_document_char_at(
            &editor->document, position + 1)))
        position += 2;
    else
        position++;
    return position;
}

static size_t editor_valid_position(const TintaEditor *editor,
                                    size_t position, bool forward) {
    size_t length = editor_length(editor);
    if (position > length) position = length;
    if (position && position < length &&
        is_high_surrogate(tinta_editor_document_char_at(
            &editor->document, position - 1)) &&
        is_low_surrogate(tinta_editor_document_char_at(
            &editor->document, position)))
        return forward ? position + 1 : position - 1;
    return position;
}

static void editor_selection(const TintaEditor *editor,
                             size_t *start, size_t *end) {
    *start = editor->anchor < editor->caret ? editor->anchor : editor->caret;
    *end = editor->anchor > editor->caret ? editor->anchor : editor->caret;
}

static size_t editor_normalized_length(const wchar_t *text, size_t length,
                                       bool *valid) {
    size_t result = 0;
    size_t i;
    *valid = text || !length;
    for (i = 0; *valid && i < length; i++) {
        if (!text[i]) {
            *valid = false;
            break;
        }
        if (text[i] == L'\r' && i + 1 < length && text[i + 1] == L'\n') i++;
        result++;
    }
    return result;
}

static void editor_clamp_scroll(TintaEditor *editor) {
    float content_width = editor->document.lines.maximum_width +
        editor->padding_left + editor->padding_right;
    float content_height = editor->document.lines.total_height +
        editor->padding_top + editor->padding_bottom;
    float max_x = fmaxf(0, content_width - editor->width);
    float max_y = fmaxf(0, content_height - editor->height);
    if (editor_wrap_enabled(editor)) max_x = 0;
    editor->scroll_x = clamp_float(editor->scroll_x, 0, max_x);
    editor->scroll_y = clamp_float(editor->scroll_y, 0, max_y);
}

static bool editor_line_text(TintaEditor *editor, size_t line,
                             wchar_t **text, size_t *length,
                             size_t *line_start) {
    TintaEditorLineMetric metric;
    size_t start;
    size_t content;
    wchar_t *buffer;
    if (!tinta_editor_document_get_line(&editor->document, line,
                                        &start, &metric))
        return false;
    content = metric.length;
    if (content && tinta_editor_document_char_at(
            &editor->document, start + content - 1) == L'\n')
        content--;
    if (content > UINT32_MAX) return false;
    buffer = (wchar_t *)malloc((content + 1) * sizeof(wchar_t));
    if (!buffer) return false;
    if (content && !tinta_editor_document_copy(
            &editor->document, start, content, buffer)) {
        free(buffer);
        return false;
    }
    buffer[content] = 0;
    *text = buffer;
    *length = content;
    if (line_start) *line_start = start;
    return true;
}

static void editor_apply_fallback(TintaEditor *editor,
                                  IDWriteTextLayout *layout) {
    IDWriteTextLayout2 *layout2 = NULL;
    if (!editor->font_fallback || !layout) return;
    if (SUCCEEDED(layout->lpVtbl->QueryInterface(
            layout, &TINTA_IID_IDWRITE_TEXT_LAYOUT2, (void **)&layout2))) {
        layout2->lpVtbl->SetFontFallback(layout2, editor->font_fallback);
        layout2->lpVtbl->Release(layout2);
    }
}

static IDWriteTextLayout *editor_create_layout(TintaEditor *editor,
                                               size_t line,
                                               wchar_t **owned_text,
                                               size_t *text_length,
                                               size_t *line_start,
                                               TintaDWriteTextMetrics *metrics) {
    wchar_t *text = NULL;
    size_t length = 0;
    size_t start = 0;
    IDWriteTextLayout *layout = NULL;
    float width;
    HRESULT hr;
    if (!editor->format || !editor_line_text(
            editor, line, &text, &length, &start))
        return NULL;
    width = editor_wrap_enabled(editor) ?
        fmaxf(1, editor->width - editor->padding_left -
                    editor->padding_right - editor_scale(editor, 2)) :
        10000000.0f;
    hr = editor->dwrite_factory->lpVtbl->CreateTextLayout(
        editor->dwrite_factory, text, (UINT32)length, editor->format,
        width, 10000000.0f, &layout);
    if (FAILED(hr) || !layout) {
        free(text);
        return NULL;
    }
    layout->lpVtbl->SetWordWrapping(layout,
        editor_wrap_enabled(editor) ? TINTA_DWRITE_WORD_WRAPPING_WRAP :
                                      TINTA_DWRITE_WORD_WRAPPING_NO_WRAP);
    editor_apply_fallback(editor, layout);
    memset(metrics, 0, sizeof(*metrics));
    if (FAILED(layout->lpVtbl->GetMetrics(layout, metrics))) {
        layout->lpVtbl->Release(layout);
        free(text);
        return NULL;
    }
    *owned_text = text;
    *text_length = length;
    if (line_start) *line_start = start;
    return layout;
}

static bool editor_measure_line(TintaEditor *editor, size_t line) {
    wchar_t *text = NULL;
    size_t length = 0;
    IDWriteTextLayout *layout;
    TintaDWriteTextMetrics metrics;
    TintaEditorLineMetric old_metric;
    float line_y = tinta_editor_document_line_y(&editor->document, line);
    bool above_view = line_y < editor->scroll_y;
    tinta_editor_document_get_line(&editor->document, line, NULL, &old_metric);
    layout = editor_create_layout(editor, line, &text, &length, NULL, &metrics);
    if (!layout) return false;
    tinta_editor_document_set_line_metric(
        &editor->document, line, metrics.widthIncludingTrailingWhitespace,
        fmaxf(editor->default_line_height, metrics.height),
        editor->layout_generation);
    if (above_view)
        editor->scroll_y +=
            fmaxf(editor->default_line_height, metrics.height) -
            old_metric.height;
    layout->lpVtbl->Release(layout);
    free(text);
    return true;
}

static void editor_schedule_layout(TintaEditor *editor) {
    if (!editor->pending_layout) {
        editor->pending_layout = PostMessageW(
            editor->hwnd, TINTA_WM_EDITOR_LAYOUT, 0, 0) != FALSE;
    }
}

static void editor_invalidate_layout(TintaEditor *editor) {
    editor->layout_generation++;
    if (!editor->layout_generation) editor->layout_generation = 1;
    editor->layout_start = tinta_editor_document_line_from_y(
        &editor->document, editor->scroll_y, NULL);
    editor->layout_cursor = editor->layout_start;
    editor->layout_wrapped = false;
    editor_schedule_layout(editor);
    InvalidateRect(editor->hwnd, NULL, FALSE);
}

static void editor_process_layout(TintaEditor *editor) {
    ULONGLONG start = GetTickCount64();
    size_t count = tinta_editor_document_line_count(&editor->document);
    editor->pending_layout = false;
    while (count && GetTickCount64() - start < 4) {
        TintaEditorLineMetric metric;
        if (editor->layout_cursor >= count) {
            if (!editor->layout_wrapped && editor->layout_start) {
                editor->layout_cursor = 0;
                editor->layout_wrapped = true;
            } else {
                break;
            }
        }
        if (editor->layout_wrapped &&
            editor->layout_cursor >= editor->layout_start)
            break;
        if (tinta_editor_document_get_line(
                &editor->document, editor->layout_cursor, NULL, &metric) &&
            metric.generation != editor->layout_generation)
            editor_measure_line(editor, editor->layout_cursor);
        editor->layout_cursor++;
    }
    editor_clamp_scroll(editor);
    InvalidateRect(editor->hwnd, NULL, FALSE);
    if ((!editor->layout_wrapped && editor->layout_start &&
         editor->layout_cursor >= count) ||
        (editor->layout_cursor < count &&
         !(editor->layout_wrapped &&
           editor->layout_cursor >= editor->layout_start)))
        editor_schedule_layout(editor);
}

static void editor_discard_device(TintaEditor *editor) {
    editor_release_unknown((void **)&editor->brush);
    editor_release_unknown((void **)&editor->render_target);
}

static bool editor_create_device(TintaEditor *editor) {
    D2D1_SIZE_U size;
    HRESULT hr;
    if (!editor->d2d_factory) return false;
    size.width = editor->width > 0 ? (UINT32)editor->width : 1;
    size.height = editor->height > 0 ? (UINT32)editor->height : 1;
    if (editor->render_target) {
        hr = editor->render_target->lpVtbl->Resize(editor->render_target, &size);
        if (SUCCEEDED(hr)) return true;
        editor_discard_device(editor);
    }
    {
        D2D1_RENDER_TARGET_PROPERTIES properties;
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_properties;
        D2D1_COLOR_F white = {1, 1, 1, 1};
        memset(&properties, 0, sizeof(properties));
        properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        properties.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
        properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
        properties.dpiX = 96;
        properties.dpiY = 96;
        properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        hwnd_properties.hwnd = editor->hwnd;
        hwnd_properties.pixelSize = size;
        hwnd_properties.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY;
        hr = editor->d2d_factory->lpVtbl->CreateHwndRenderTarget(
            editor->d2d_factory, &properties, &hwnd_properties,
            &editor->render_target);
        if (FAILED(hr)) return false;
        hr = editor->render_target->lpVtbl->CreateSolidColorBrush(
            editor->render_target, &white, NULL, &editor->brush);
        if (FAILED(hr)) {
            editor_discard_device(editor);
            return false;
        }
        editor->render_target->lpVtbl->SetTextAntialiasMode(
            editor->render_target, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }
    return true;
}

static void editor_update_system_colors(TintaEditor *editor) {
    if (!editor->use_system_theme) return;
    editor->background_color = editor_colorref(GetSysColor(COLOR_WINDOW));
    editor->text_color = editor_colorref(GetSysColor(COLOR_WINDOWTEXT));
    editor->selection_color = editor_colorref(GetSysColor(COLOR_HIGHLIGHT));
    editor->selection_text_color =
        editor_colorref(GetSysColor(COLOR_HIGHLIGHTTEXT));
}

static bool editor_system_log_font(TintaEditor *editor, LOGFONTW *font) {
    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,
            sizeof(metrics), &metrics, 0,
            (UINT)lroundf(editor->dpi_scale * 96.0f)) ||
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,
                              sizeof(metrics), &metrics, 0)) {
        *font = metrics.lfMessageFont;
        return true;
    }
    return false;
}

static bool editor_update_format(TintaEditor *editor) {
    IDWriteTextFormat *format = NULL;
    const wchar_t *family = editor->log_font.lfFaceName[0] ?
        editor->log_font.lfFaceName : L"Segoe UI";
    float size = (float)abs(editor->log_font.lfHeight);
    TintaDWriteFontWeight weight;
    TintaDWriteFontStyle style;
    LONG_PTR window_style;
    TintaDWriteTextMetrics metrics;
    IDWriteTextLayout *probe = NULL;
    if (size < 1) size = 16.0f * editor->dpi_scale;
    weight = editor->log_font.lfWeight >= FW_BOLD ?
        TINTA_DWRITE_FONT_WEIGHT_BOLD : TINTA_DWRITE_FONT_WEIGHT_NORMAL;
    style = editor->log_font.lfItalic ? TINTA_DWRITE_FONT_STYLE_ITALIC :
                                       TINTA_DWRITE_FONT_STYLE_NORMAL;
    if (FAILED(editor->dwrite_factory->lpVtbl->CreateTextFormat(
            editor->dwrite_factory, family, NULL, weight, style,
            TINTA_DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format)))
        return false;
    format->lpVtbl->SetParagraphAlignment(
        format, TINTA_DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->lpVtbl->SetIncrementalTabStop(
        format, editor->tab_width > 0.0f ? editor->tab_width :
                                          editor_scale(editor, 32));
    window_style = GetWindowLongPtrW(editor->hwnd, GWL_STYLE);
    format->lpVtbl->SetTextAlignment(format,
        (window_style & ES_CENTER) ? TINTA_DWRITE_TEXT_ALIGNMENT_CENTER :
        (window_style & ES_RIGHT) ? TINTA_DWRITE_TEXT_ALIGNMENT_TRAILING :
                                    TINTA_DWRITE_TEXT_ALIGNMENT_LEADING);
    editor_release_unknown((void **)&editor->format);
    editor->format = format;
    if (SUCCEEDED(editor->dwrite_factory->lpVtbl->CreateTextLayout(
            editor->dwrite_factory, L"Mg", 2, format, 1000, 1000, &probe))) {
        editor_apply_fallback(editor, probe);
        memset(&metrics, 0, sizeof(metrics));
        if (SUCCEEDED(probe->lpVtbl->GetMetrics(probe, &metrics)))
            editor->default_line_height = fmaxf(1, metrics.height);
        probe->lpVtbl->Release(probe);
    }
    editor->padding_left = editor_scale(editor, 4);
    editor->padding_right = editor_scale(editor, 4);
    editor->padding_top = editor_scale(editor, 3);
    editor->padding_bottom = editor_scale(editor, 3);
    editor_invalidate_layout(editor);
    return true;
}

static void editor_refresh_owned_font(TintaEditor *editor) {
    HFONT font = CreateFontIndirectW(&editor->log_font);
    if (!font) return;
    if (editor->own_font && editor->font) DeleteObject(editor->font);
    editor->font = font;
    editor->own_font = true;
}

static bool editor_set_font(TintaEditor *editor, HFONT font, bool redraw) {
    LOGFONTW log_font;
    bool same_font;
    if (!font) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!GetObjectW(font, sizeof(log_font), &log_font)) return false;
    same_font = font == editor->font;
    if (!same_font && editor->own_font && editor->font)
        DeleteObject(editor->font);
    editor->font = font;
    if (!same_font) editor->own_font = false;
    editor->log_font = log_font;
    if (!editor_update_format(editor)) return false;
    if (redraw) InvalidateRect(editor->hwnd, NULL, FALSE);
    return true;
}

static bool editor_scrollbar(TintaEditor *editor, bool horizontal,
                             TintaEditorScrollbar *scrollbar) {
    float content = horizontal ?
        editor->document.lines.maximum_width + editor->padding_left +
            editor->padding_right :
        editor->document.lines.total_height + editor->padding_top +
            editor->padding_bottom;
    float viewport = horizontal ? (float)editor->width : (float)editor->height;
    float thickness = editor_scale(editor, 12);
    float thumb_size;
    float scroll;
    memset(scrollbar, 0, sizeof(*scrollbar));
    if ((horizontal && (!editor->allow_horizontal_overlay ||
                        editor_wrap_enabled(editor))) ||
        (!horizontal && !editor->allow_vertical_overlay) ||
        content <= viewport || viewport <= 0)
        return false;
    scrollbar->visible = true;
    scrollbar->maximum = content - viewport;
    if (horizontal) {
        scrollbar->track.left = editor_scale(editor, 2);
        scrollbar->track.right = editor->width - editor_scale(editor, 2);
        scrollbar->track.top = (float)editor->height - thickness;
        scrollbar->track.bottom = (float)editor->height;
    } else {
        scrollbar->track.left = (float)editor->width - thickness;
        scrollbar->track.right = (float)editor->width;
        scrollbar->track.top = editor_scale(editor, 2);
        scrollbar->track.bottom = (float)editor->height - editor_scale(editor, 2);
    }
    thumb_size = fmaxf(editor_scale(editor, 24),
        (horizontal ? scrollbar->track.right - scrollbar->track.left :
                      scrollbar->track.bottom - scrollbar->track.top) *
        viewport / content);
    scrollbar->travel = (horizontal ?
        scrollbar->track.right - scrollbar->track.left :
        scrollbar->track.bottom - scrollbar->track.top) - thumb_size;
    scroll = horizontal ? editor->scroll_x : editor->scroll_y;
    if (horizontal) {
        scrollbar->thumb.left = scrollbar->track.left +
            (scrollbar->maximum ? scroll / scrollbar->maximum : 0) *
            scrollbar->travel;
        scrollbar->thumb.right = scrollbar->thumb.left + thumb_size;
        scrollbar->thumb.top = editor->height -
            ((editor->h_scrollbar_hovered || editor->h_scrollbar_dragging) ?
             editor_scale(editor, 9) : editor_scale(editor, 5));
        scrollbar->thumb.bottom = editor->height - editor_scale(editor, 2);
    } else {
        scrollbar->thumb.top = scrollbar->track.top +
            (scrollbar->maximum ? scroll / scrollbar->maximum : 0) *
            scrollbar->travel;
        scrollbar->thumb.bottom = scrollbar->thumb.top + thumb_size;
        scrollbar->thumb.left = editor->width -
            ((editor->v_scrollbar_hovered || editor->v_scrollbar_dragging) ?
             editor_scale(editor, 9) : editor_scale(editor, 5));
        scrollbar->thumb.right = editor->width - editor_scale(editor, 2);
    }
    return true;
}

static bool editor_position_to_point(TintaEditor *editor, size_t position,
                                     POINT *point);

static void editor_position_caret(TintaEditor *editor) {
    POINT point;
    if (!editor->focused || !editor->format) return;
    if (editor_position_to_point(editor, editor->caret, &point))
        SetCaretPos(point.x, point.y);
}

static bool editor_position_to_point(TintaEditor *editor, size_t position,
                                     POINT *point) {
    size_t line;
    size_t start;
    TintaEditorLineMetric line_metric;
    wchar_t *text = NULL;
    size_t length = 0;
    IDWriteTextLayout *layout;
    TintaDWriteTextMetrics metrics;
    TintaDWriteHitTestMetrics hit;
    float x = 0;
    float y = 0;
    size_t offset;
    if (!point || !editor->format) return false;
    position = editor_valid_position(editor, position, false);
    line = tinta_editor_document_line_from_position(
        &editor->document, position);
    if (!tinta_editor_document_get_line(
            &editor->document, line, &start, &line_metric))
        return false;
    offset = position - start;
    if (offset && offset == line_metric.length &&
        tinta_editor_document_char_at(&editor->document,
                                      position - 1) == L'\n')
        offset--;
    layout = editor_create_layout(editor, line, &text, &length, NULL, &metrics);
    if (layout) {
        if (offset > length) offset = length;
        memset(&hit, 0, sizeof(hit));
        if (SUCCEEDED(layout->lpVtbl->HitTestTextPosition(
                layout, (UINT32)offset, FALSE, &x, &y, &hit))) {
            x += editor->padding_left - editor->scroll_x;
            y += editor->padding_top +
                tinta_editor_document_line_y(&editor->document, line) -
                editor->scroll_y;
            point->x = (LONG)lroundf(x);
            point->y = (LONG)lroundf(y);
            layout->lpVtbl->Release(layout);
            free(text);
            return true;
        }
        layout->lpVtbl->Release(layout);
    }
    free(text);
    return false;
}

static void editor_reveal_caret(TintaEditor *editor) {
    size_t line = tinta_editor_document_line_from_position(
        &editor->document, editor->caret);
    float line_y = tinta_editor_document_line_y(&editor->document, line);
    TintaEditorLineMetric metric;
    float old_x = editor->scroll_x;
    float old_y = editor->scroll_y;
    wchar_t *text = NULL;
    size_t length = 0;
    size_t start = 0;
    IDWriteTextLayout *layout;
    TintaDWriteTextMetrics metrics;
    TintaDWriteHitTestMetrics hit;
    float x = 0, y = 0;
    tinta_editor_document_get_line(&editor->document, line, NULL, &metric);
    if (line_y < editor->scroll_y)
        editor->scroll_y = line_y;
    else if (line_y + metric.height > editor->scroll_y + editor->height -
             editor->padding_bottom)
        editor->scroll_y = line_y + metric.height - editor->height +
                           editor->padding_bottom;
    layout = editor_create_layout(editor, line, &text, &length, &start, &metrics);
    if (layout) {
        size_t offset = editor->caret - start;
        if (offset > length) offset = length;
        memset(&hit, 0, sizeof(hit));
        if (SUCCEEDED(layout->lpVtbl->HitTestTextPosition(
                layout, (UINT32)offset, FALSE, &x, &y, &hit)) &&
            !editor_wrap_enabled(editor)) {
            if (x < editor->scroll_x)
                editor->scroll_x = x;
            else if (x + editor_scale(editor, 4) >
                     editor->scroll_x + editor->width -
                     editor->padding_right)
                editor->scroll_x = x + editor_scale(editor, 4) -
                    editor->width + editor->padding_right;
        }
        layout->lpVtbl->Release(layout);
    }
    free(text);
    editor_clamp_scroll(editor);
    if (old_x != editor->scroll_x) editor_notify(editor, EN_HSCROLL);
    if (old_y != editor->scroll_y) editor_notify(editor, EN_VSCROLL);
    editor_position_caret(editor);
}

static void editor_selection_changed(TintaEditor *editor, bool reveal) {
    if (reveal) editor_reveal_caret(editor);
    InvalidateRect(editor->hwnd, NULL, FALSE);
    tinta_editor_uia_raise_selection_changed(editor);
}

static bool editor_replace(TintaEditor *editor, size_t start, size_t end,
                           const wchar_t *text, size_t length,
                           bool record_undo) {
    bool valid;
    size_t normalized = editor_normalized_length(text, length, &valid);
    TintaEditorReplaceResult result;
    if (!valid || editor->read_only) return false;
    start = editor_valid_position(editor, start, false);
    end = editor_valid_position(editor, end, true);
    if (start > end) return false;
    if (start == end && !normalized) return true;
    if (!tinta_editor_document_replace(
            &editor->document, start, end, text, length, true,
            editor->anchor, editor->caret,
            start + normalized, start + normalized,
            editor->default_line_height, record_undo, &result)) {
        if (editor_length(editor) - (end - start) + normalized >
            editor->document.text_limit)
            editor_notify(editor, EN_MAXTEXT);
        else
            editor_notify(editor, EN_ERRSPACE);
        return false;
    }
    editor->anchor = editor->caret = start + result.inserted_length;
    editor->desired_x = -1;
    editor->layout_cursor = tinta_editor_document_line_from_position(
        &editor->document, start);
    editor->layout_start = editor->layout_cursor;
    editor->layout_wrapped = false;
    editor_schedule_layout(editor);
    editor_notify(editor, EN_UPDATE);
    editor_notify(editor, EN_CHANGE);
    tinta_editor_uia_raise_text_changed(editor);
    editor_selection_changed(editor, true);
    return true;
}

static bool editor_replace_selection(TintaEditor *editor,
                                     const wchar_t *text, size_t length,
                                     bool record_undo) {
    size_t start, end;
    editor->undo_group = 0;
    editor_selection(editor, &start, &end);
    return editor_replace(editor, start, end, text, length, record_undo);
}

static bool editor_copy_selection(TintaEditor *editor) {
    size_t start, end;
    wchar_t *text;
    bool result;
    editor_selection(editor, &start, &end);
    if (start == end) return false;
    text = (wchar_t *)malloc((end - start + 1) * sizeof(wchar_t));
    if (!text) return false;
    result = tinta_editor_document_copy(
        &editor->document, start, end - start, text);
    if (result) result = tinta_set_clipboard_text(
        editor->hwnd, text, end - start);
    free(text);
    return result;
}

static bool editor_paste(TintaEditor *editor) {
    TintaStr16 text = {0};
    bool result;
    if (editor->read_only || !tinta_get_clipboard_text(editor->hwnd, &text))
        return false;
    result = editor_replace_selection(editor, text.data, text.len, true);
    tinta_str16_destroy(&text);
    return result;
}

static bool editor_undo(TintaEditor *editor) {
    editor->undo_group = 0;
    if (editor->read_only || !tinta_editor_document_undo(
            &editor->document, &editor->anchor, &editor->caret,
            editor->default_line_height))
        return false;
    editor->layout_cursor = 0;
    editor->layout_start = 0;
    editor->layout_wrapped = false;
    editor_schedule_layout(editor);
    editor_notify(editor, EN_UPDATE);
    editor_notify(editor, EN_CHANGE);
    tinta_editor_uia_raise_text_changed(editor);
    editor_selection_changed(editor, true);
    return true;
}

static bool editor_redo(TintaEditor *editor) {
    editor->undo_group = 0;
    if (editor->read_only || !tinta_editor_document_redo(
            &editor->document, &editor->anchor, &editor->caret,
            editor->default_line_height))
        return false;
    editor->layout_cursor = 0;
    editor->layout_start = 0;
    editor->layout_wrapped = false;
    editor_schedule_layout(editor);
    editor_notify(editor, EN_UPDATE);
    editor_notify(editor, EN_CHANGE);
    tinta_editor_uia_raise_text_changed(editor);
    editor_selection_changed(editor, true);
    return true;
}

static int editor_character_class(wchar_t character) {
    if (iswalnum(character) || character == L'_') return 1;
    if (iswspace(character)) return 2;
    return 3;
}

static size_t editor_word_left(TintaEditor *editor, size_t position) {
    int character_class;
    if (!position) return 0;
    position = editor_previous_position(editor, position);
    while (position && iswspace(tinta_editor_document_char_at(
            &editor->document, position)))
        position = editor_previous_position(editor, position);
    character_class = editor_character_class(tinta_editor_document_char_at(
        &editor->document, position));
    while (position) {
        size_t previous = editor_previous_position(editor, position);
        if (editor_character_class(tinta_editor_document_char_at(
                &editor->document, previous)) != character_class)
            break;
        position = previous;
    }
    return position;
}

static size_t editor_word_right(TintaEditor *editor, size_t position) {
    size_t length = editor_length(editor);
    int character_class;
    if (position >= length) return length;
    character_class = editor_character_class(tinta_editor_document_char_at(
        &editor->document, position));
    while (position < length && editor_character_class(
            tinta_editor_document_char_at(&editor->document, position)) ==
            character_class)
        position = editor_next_position(editor, position);
    while (position < length && iswspace(tinta_editor_document_char_at(
            &editor->document, position)))
        position = editor_next_position(editor, position);
    return position;
}

static size_t editor_position_from_point(TintaEditor *editor, int x, int y) {
    float document_y = y + editor->scroll_y - editor->padding_top;
    float line_y = 0;
    size_t line = tinta_editor_document_line_from_y(
        &editor->document, document_y, &line_y);
    wchar_t *text = NULL;
    size_t length = 0;
    size_t start = 0;
    IDWriteTextLayout *layout;
    TintaDWriteTextMetrics metrics;
    TintaDWriteHitTestMetrics hit;
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    size_t result;
    layout = editor_create_layout(editor, line, &text, &length, &start, &metrics);
    if (!layout) return start;
    memset(&hit, 0, sizeof(hit));
    if (SUCCEEDED(layout->lpVtbl->HitTestPoint(
            layout, x + editor->scroll_x - editor->padding_left,
            document_y - line_y, &trailing, &inside, &hit))) {
        result = start + hit.textPosition + (trailing ? hit.length : 0);
        if (result > start + length) result = start + length;
    } else {
        result = x < editor->padding_left ? start : start + length;
    }
    layout->lpVtbl->Release(layout);
    free(text);
    return editor_valid_position(editor, result, trailing != FALSE);
}

static void editor_select_word(TintaEditor *editor, size_t position) {
    size_t start = position;
    size_t end = position;
    int character_class = 0;
    if (position < editor_length(editor))
        character_class = editor_character_class(
            tinta_editor_document_char_at(&editor->document, position));
    else if (position) {
        size_t previous = editor_previous_position(editor, position);
        character_class = editor_character_class(
            tinta_editor_document_char_at(&editor->document, previous));
        start = previous;
    }
    while (start) {
        size_t previous = editor_previous_position(editor, start);
        if (editor_character_class(tinta_editor_document_char_at(
                &editor->document, previous)) != character_class)
            break;
        start = previous;
    }
    while (end < editor_length(editor) && editor_character_class(
            tinta_editor_document_char_at(&editor->document, end)) ==
            character_class)
        end = editor_next_position(editor, end);
    editor->anchor = start;
    editor->caret = end;
}

static void editor_line_range(TintaEditor *editor, size_t position,
                              size_t *start, size_t *end) {
    size_t line = tinta_editor_document_line_from_position(
        &editor->document, position);
    TintaEditorLineMetric metric;
    tinta_editor_document_get_line(&editor->document, line, start, &metric);
    *end = *start + metric.length;
}

static bool editor_click_near(TintaEditor *editor, int x, int y,
                              ULONGLONG now) {
    return now - editor->last_click_tick <= GetDoubleClickTime() &&
        abs(x - editor->last_click_point.x) <=
            GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
        abs(y - editor->last_click_point.y) <=
            GetSystemMetrics(SM_CYDOUBLECLK) / 2;
}

static void editor_update_click(TintaEditor *editor, UINT message,
                                int x, int y) {
    ULONGLONG now = GetTickCount64();
    if (message == WM_LBUTTONDBLCLK && editor_click_near(editor, x, y, now))
        editor->click_count = 2;
    else if (message == WM_LBUTTONDOWN &&
             editor_click_near(editor, x, y, now))
        editor->click_count++;
    else
        editor->click_count = 1;
    if (editor->click_count > 3) editor->click_count = 1;
    editor->last_click_tick = now;
    editor->last_click_point.x = x;
    editor->last_click_point.y = y;
}

static bool point_in_rect(D2D1_RECT_F rect, int x, int y) {
    return x >= rect.left && x <= rect.right &&
           y >= rect.top && y <= rect.bottom;
}

static bool editor_begin_scrollbar(TintaEditor *editor, int x, int y) {
    TintaEditorScrollbar scrollbar;
    if (editor_scrollbar(editor, false, &scrollbar) &&
        point_in_rect(scrollbar.track, x, y)) {
        editor->click_count = 0;
        if (!point_in_rect(scrollbar.thumb, x, y) && scrollbar.travel > 0) {
            float top = clamp_float(y -
                (scrollbar.thumb.bottom - scrollbar.thumb.top) / 2,
                scrollbar.track.top, scrollbar.track.top + scrollbar.travel);
            editor->scroll_y = (top - scrollbar.track.top) /
                scrollbar.travel * scrollbar.maximum;
        }
        editor->v_scrollbar_dragging = true;
        editor->scrollbar_drag_origin = (float)y;
        editor->scrollbar_drag_scroll = editor->scroll_y;
        SetCapture(editor->hwnd);
        InvalidateRect(editor->hwnd, NULL, FALSE);
        return true;
    }
    if (editor_scrollbar(editor, true, &scrollbar) &&
        point_in_rect(scrollbar.track, x, y)) {
        editor->click_count = 0;
        if (!point_in_rect(scrollbar.thumb, x, y) && scrollbar.travel > 0) {
            float left = clamp_float(x -
                (scrollbar.thumb.right - scrollbar.thumb.left) / 2,
                scrollbar.track.left, scrollbar.track.left + scrollbar.travel);
            editor->scroll_x = (left - scrollbar.track.left) /
                scrollbar.travel * scrollbar.maximum;
        }
        editor->h_scrollbar_dragging = true;
        editor->scrollbar_drag_origin = (float)x;
        editor->scrollbar_drag_scroll = editor->scroll_x;
        SetCapture(editor->hwnd);
        InvalidateRect(editor->hwnd, NULL, FALSE);
        return true;
    }
    return false;
}

static void editor_drag_scrollbar(TintaEditor *editor, int x, int y) {
    TintaEditorScrollbar scrollbar;
    if (editor->v_scrollbar_dragging &&
        editor_scrollbar(editor, false, &scrollbar) && scrollbar.travel > 0) {
        editor->scroll_y = clamp_float(editor->scrollbar_drag_scroll +
            (y - editor->scrollbar_drag_origin) /
                scrollbar.travel * scrollbar.maximum,
            0, scrollbar.maximum);
        editor_notify(editor, EN_VSCROLL);
    } else if (editor->h_scrollbar_dragging &&
               editor_scrollbar(editor, true, &scrollbar) &&
               scrollbar.travel > 0) {
        editor->scroll_x = clamp_float(editor->scrollbar_drag_scroll +
            (x - editor->scrollbar_drag_origin) /
                scrollbar.travel * scrollbar.maximum,
            0, scrollbar.maximum);
        editor_notify(editor, EN_HSCROLL);
    }
    InvalidateRect(editor->hwnd, NULL, FALSE);
    editor_position_caret(editor);
}

static void editor_update_scrollbar_hover(TintaEditor *editor, int x, int y) {
    TintaEditorScrollbar scrollbar;
    bool vertical = editor_scrollbar(editor, false, &scrollbar) &&
                    point_in_rect(scrollbar.track, x, y);
    bool horizontal = editor_scrollbar(editor, true, &scrollbar) &&
                      point_in_rect(scrollbar.track, x, y);
    if (vertical != editor->v_scrollbar_hovered ||
        horizontal != editor->h_scrollbar_hovered) {
        editor->v_scrollbar_hovered = vertical;
        editor->h_scrollbar_hovered = horizontal;
        InvalidateRect(editor->hwnd, NULL, FALSE);
    }
}

static void editor_stop_autoscroll(TintaEditor *editor) {
    editor->auto_scroll_x = 0;
    editor->auto_scroll_y = 0;
    KillTimer(editor->hwnd, TINTA_TIMER_EDITOR_AUTOSCROLL);
}

static void editor_update_autoscroll(TintaEditor *editor, int x, int y) {
    int horizontal = x < 0 ? -1 : x >= editor->width ? 1 : 0;
    int vertical = y < 0 ? -1 : y >= editor->height ? 1 : 0;
    if (editor_wrap_enabled(editor)) horizontal = 0;
    editor->auto_scroll_x = horizontal;
    editor->auto_scroll_y = vertical;
    if (horizontal || vertical)
        SetTimer(editor->hwnd, TINTA_TIMER_EDITOR_AUTOSCROLL, 30, NULL);
    else
        KillTimer(editor->hwnd, TINTA_TIMER_EDITOR_AUTOSCROLL);
}

static void editor_extend_mouse_selection(TintaEditor *editor, int x, int y) {
    if (editor->line_selecting) {
        size_t position = editor_position_from_point(editor, x, y);
        size_t line = tinta_editor_document_line_from_position(
            &editor->document, position);
        size_t start, end;
        if (line >= editor->line_selection_anchor) {
            editor_line_range(editor,
                tinta_editor_document_line_start(
                    &editor->document, editor->line_selection_anchor),
                &start, &end);
            editor->anchor = start;
            editor_line_range(editor, position, &start, &end);
            editor->caret = end;
        } else {
            editor_line_range(editor,
                tinta_editor_document_line_start(
                    &editor->document, editor->line_selection_anchor),
                &start, &end);
            editor->anchor = end;
            editor_line_range(editor, position, &start, &end);
            editor->caret = start;
        }
        editor_selection_changed(editor, false);
    } else if (editor->selecting) {
        editor->caret = editor_position_from_point(editor, x, y);
        editor_selection_changed(editor, false);
    }
    if (editor->line_selecting || editor->selecting)
        editor_update_autoscroll(editor, x, y);
}

static void editor_autoscroll(TintaEditor *editor) {
    POINT point;
    float old_x = editor->scroll_x;
    float old_y = editor->scroll_y;
    if (!editor->selecting && !editor->line_selecting) {
        editor_stop_autoscroll(editor);
        return;
    }
    editor->scroll_x += editor->auto_scroll_x * editor_scale(editor, 16);
    editor->scroll_y += editor->auto_scroll_y * editor->default_line_height;
    editor_clamp_scroll(editor);
    if (old_x != editor->scroll_x) editor_notify(editor, EN_HSCROLL);
    if (old_y != editor->scroll_y) editor_notify(editor, EN_VSCROLL);
    GetCursorPos(&point);
    ScreenToClient(editor->hwnd, &point);
    editor_extend_mouse_selection(editor, point.x, point.y);
    InvalidateRect(editor->hwnd, NULL, FALSE);
}

static void editor_scroll_command(TintaEditor *editor, bool horizontal,
                                  WPARAM wparam) {
    float *value = horizontal ? &editor->scroll_x : &editor->scroll_y;
    float old = *value;
    float line = horizontal ? editor_scale(editor, 16) :
                              editor->default_line_height;
    float page = horizontal ? (float)editor->width : (float)editor->height;
    float content = horizontal ?
        editor->document.lines.maximum_width + editor->padding_left +
            editor->padding_right :
        editor->document.lines.total_height + editor->padding_top +
            editor->padding_bottom;
    float maximum = fmaxf(0.0f, content - page);
    switch (LOWORD(wparam)) {
        case SB_LINEUP: *value -= line; break;
        case SB_LINEDOWN: *value += line; break;
        case SB_PAGEUP: *value -= page; break;
        case SB_PAGEDOWN: *value += page; break;
        case SB_TOP: *value = 0.0f; break;
        case SB_BOTTOM: *value = maximum; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            *value = maximum * HIWORD(wparam) / 65535.0f;
            break;
        default: break;
    }
    editor_clamp_scroll(editor);
    if (*value != old)
        editor_notify(editor, horizontal ? EN_HSCROLL : EN_VSCROLL);
    InvalidateRect(editor->hwnd, NULL, FALSE);
    editor_position_caret(editor);
}

static void editor_set_selection(TintaEditor *editor,
                                 size_t anchor, size_t caret,
                                 bool reveal) {
    editor->undo_group = 0;
    editor->anchor = editor_valid_position(editor, anchor, false);
    editor->caret = editor_valid_position(editor, caret, true);
    editor->desired_x = -1;
    editor_selection_changed(editor, reveal);
}

static void editor_move_vertical(TintaEditor *editor, int direction,
                                 bool extend) {
    size_t line = tinta_editor_document_line_from_position(
        &editor->document, editor->caret);
    size_t target;
    float x = editor->desired_x;
    int caret_x = 0;
    int caret_y = 0;
    if (x < 0) {
        POINT point = {0};
        editor_position_to_point(editor, editor->caret, &point);
        caret_x = point.x;
        caret_y = point.y;
        (void)caret_y;
        x = (float)caret_x;
        editor->desired_x = x;
    }
    if (direction < 0 && line)
        target = line - 1;
    else if (direction > 0 && line + 1 <
             tinta_editor_document_line_count(&editor->document))
        target = line + 1;
    else
        target = line;
    if (!extend) editor->anchor = editor->caret;
    editor->caret = editor_position_from_point(editor, (int)x,
        (int)(editor->padding_top +
              tinta_editor_document_line_y(&editor->document, target) -
              editor->scroll_y + editor->default_line_height / 2));
    if (!extend) editor->anchor = editor->caret;
    editor_selection_changed(editor, true);
}

static bool control_pressed(void) {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

static bool shift_pressed(void) {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

static void editor_handle_key(TintaEditor *editor, WPARAM key) {
    bool control = control_pressed();
    bool extend = shift_pressed();
    size_t start, end;
    size_t old_caret = editor->caret;
    unsigned int previous_group = editor->undo_group;
    editor->undo_group = 0;
    editor->pending_high_surrogate = false;
    editor->click_count = 0;
    editor->line_selecting = false;
    if (control) {
        switch (key) {
            case 'A': editor_set_selection(editor, 0, editor_length(editor), true); return;
            case 'C': editor_copy_selection(editor); return;
            case 'X':
                if (!editor->read_only && editor_copy_selection(editor))
                    editor_replace_selection(editor, L"", 0, true);
                return;
            case 'V': editor_paste(editor); return;
            case 'Z':
                if (extend) editor_redo(editor); else editor_undo(editor);
                return;
            case 'Y': editor_redo(editor); return;
            case VK_HOME: editor->caret = 0; break;
            case VK_END: editor->caret = editor_length(editor); break;
            case VK_LEFT: editor->caret = editor_word_left(editor, editor->caret); break;
            case VK_RIGHT: editor->caret = editor_word_right(editor, editor->caret); break;
            default: return;
        }
    } else {
        switch (key) {
            case VK_LEFT:
                editor_selection(editor, &start, &end);
                editor->caret = !extend && start != end ? start :
                    editor_previous_position(editor, editor->caret);
                break;
            case VK_RIGHT:
                editor_selection(editor, &start, &end);
                editor->caret = !extend && start != end ? end :
                    editor_next_position(editor, editor->caret);
                break;
            case VK_UP: editor_move_vertical(editor, -1, extend); return;
            case VK_DOWN: editor_move_vertical(editor, 1, extend); return;
            case VK_HOME: {
                size_t line = tinta_editor_document_line_from_position(
                    &editor->document, editor->caret);
                editor->caret = tinta_editor_document_line_start(
                    &editor->document, line);
                break;
            }
            case VK_END: {
                size_t line = tinta_editor_document_line_from_position(
                    &editor->document, editor->caret);
                size_t line_start;
                TintaEditorLineMetric metric;
                tinta_editor_document_get_line(&editor->document, line,
                                                &line_start, &metric);
                editor->caret = line_start + metric.length;
                if (editor->caret && editor->caret <= editor_length(editor) &&
                    tinta_editor_document_char_at(&editor->document,
                                                  editor->caret - 1) == L'\n')
                    editor->caret--;
                break;
            }
            case VK_PRIOR:
                editor->scroll_y = fmaxf(0, editor->scroll_y - editor->height);
                editor->caret = editor_position_from_point(
                    editor, (int)(editor->desired_x >= 0 ? editor->desired_x :
                                  editor->padding_left),
                    (int)editor->padding_top);
                break;
            case VK_NEXT:
                editor->scroll_y += editor->height;
                editor_clamp_scroll(editor);
                editor->caret = editor_position_from_point(
                    editor, (int)(editor->desired_x >= 0 ? editor->desired_x :
                                  editor->padding_left),
                    editor->height - (int)editor->padding_bottom);
                break;
            case VK_BACK:
                if (!editor->read_only) {
                    bool collapsed;
                    editor_selection(editor, &start, &end);
                    collapsed = start == end;
                    if (start == end) start = editor_previous_position(editor, start);
                    if (editor_replace(editor, start, end, L"", 0, true) &&
                        collapsed && start != end) {
                        if (previous_group == TINTA_EDITOR_UNDO_COALESCE_BACKSPACE)
                            tinta_editor_document_coalesce_last(
                                &editor->document,
                                TINTA_EDITOR_UNDO_COALESCE_BACKSPACE);
                        editor->undo_group =
                            TINTA_EDITOR_UNDO_COALESCE_BACKSPACE;
                    }
                }
                return;
            case VK_DELETE:
                if (!editor->read_only) {
                    bool collapsed;
                    editor_selection(editor, &start, &end);
                    collapsed = start == end;
                    if (start == end) end = editor_next_position(editor, end);
                    if (editor_replace(editor, start, end, L"", 0, true) &&
                        collapsed && start != end) {
                        if (previous_group == TINTA_EDITOR_UNDO_COALESCE_DELETE)
                            tinta_editor_document_coalesce_last(
                                &editor->document,
                                TINTA_EDITOR_UNDO_COALESCE_DELETE);
                        editor->undo_group =
                            TINTA_EDITOR_UNDO_COALESCE_DELETE;
                    }
                }
                return;
            case VK_TAB:
                if (!editor->read_only)
                    editor_replace_selection(editor, L"\t", 1, true);
                return;
            default: return;
        }
    }
    editor->caret = editor_valid_position(editor, editor->caret,
                                           editor->caret >= old_caret);
    if (!extend) editor->anchor = editor->caret;
    editor->desired_x = -1;
    editor_selection_changed(editor, true);
}

static void editor_handle_char(TintaEditor *editor, wchar_t character) {
    wchar_t text[2];
    size_t length = 1;
    size_t start, end;
    unsigned int previous_group = editor->undo_group;
    editor->undo_group = 0;
    editor->click_count = 0;
    if (editor->read_only || character == 8 || character == 9 ||
        character == 27)
        return;
    if (character == L'\r') character = L'\n';
    if (is_high_surrogate(character)) {
        editor->pending_high_surrogate = true;
        editor->high_surrogate = character;
        return;
    }
    if (is_low_surrogate(character) && editor->pending_high_surrogate) {
        text[0] = editor->high_surrogate;
        text[1] = character;
        length = 2;
        editor->pending_high_surrogate = false;
    } else {
        editor->pending_high_surrogate = false;
        text[0] = character;
    }
    editor_selection(editor, &start, &end);
    if (editor_replace(editor, start, end, text, length, true) && start == end) {
        if (previous_group == TINTA_EDITOR_UNDO_COALESCE_TYPING)
            tinta_editor_document_coalesce_last(
                &editor->document, TINTA_EDITOR_UNDO_COALESCE_TYPING);
        editor->undo_group = TINTA_EDITOR_UNDO_COALESCE_TYPING;
    }
}

static void editor_draw_selection(TintaEditor *editor,
                                  IDWriteTextLayout *layout,
                                  D2D1_POINT_2F origin,
                                  size_t start, size_t length,
                                  bool selected_text) {
    TintaDWriteHitTestMetrics stack[16];
    TintaDWriteHitTestMetrics *hits = stack;
    UINT32 count = 0;
    HRESULT hr;
    UINT32 i;
    if (!length) return;
    hr = layout->lpVtbl->HitTestTextRange(
        layout, (UINT32)start, (UINT32)length, origin.x, origin.y,
        hits, 16, &count);
    if (hr == E_NOT_SUFFICIENT_BUFFER && count > 16) {
        hits = (TintaDWriteHitTestMetrics *)malloc(count * sizeof(*hits));
        if (!hits) return;
        hr = layout->lpVtbl->HitTestTextRange(
            layout, (UINT32)start, (UINT32)length, origin.x, origin.y,
            hits, count, &count);
    }
    if (SUCCEEDED(hr)) {
        for (i = 0; i < count; i++) {
            D2D1_RECT_F rect = {
                hits[i].left, hits[i].top,
                hits[i].left + hits[i].width,
                hits[i].top + hits[i].height
            };
            if (!selected_text) {
                editor_set_brush(editor, editor->selection_color, 1);
                editor->render_target->lpVtbl->FillRectangle(
                    editor->render_target, &rect, (ID2D1Brush *)editor->brush);
            } else {
                editor->render_target->lpVtbl->PushAxisAlignedClip(
                    editor->render_target, &rect, D2D1_ANTIALIAS_MODE_ALIASED);
                editor_set_brush(editor, editor->selection_text_color, 1);
                editor->render_target->lpVtbl->DrawTextLayout(
                    editor->render_target, origin, layout,
                    (ID2D1Brush *)editor->brush,
                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                editor->render_target->lpVtbl->PopAxisAlignedClip(
                    editor->render_target);
            }
        }
    }
    if (hits != stack) free(hits);
}

static void editor_draw_scrollbars(TintaEditor *editor) {
    TintaEditorScrollbar scrollbar;
    D2D1_ROUNDED_RECT rounded;
    if (editor_scrollbar(editor, false, &scrollbar)) {
        rounded.rect = scrollbar.thumb;
        rounded.radiusX = rounded.radiusY = editor_scale(editor, 4);
        editor_set_brush(editor, editor->text_color,
            editor->v_scrollbar_hovered || editor->v_scrollbar_dragging ?
            0.5f : 0.28f);
        editor->render_target->lpVtbl->FillRoundedRectangle(
            editor->render_target, &rounded, (ID2D1Brush *)editor->brush);
    }
    if (editor_scrollbar(editor, true, &scrollbar)) {
        rounded.rect = scrollbar.thumb;
        rounded.radiusX = rounded.radiusY = editor_scale(editor, 4);
        editor_set_brush(editor, editor->text_color,
            editor->h_scrollbar_hovered || editor->h_scrollbar_dragging ?
            0.5f : 0.28f);
        editor->render_target->lpVtbl->FillRoundedRectangle(
            editor->render_target, &rounded, (ID2D1Brush *)editor->brush);
    }
}

static void editor_render(TintaEditor *editor) {
    D2D1_COLOR_F background;
    D2D1_RECT_F clip;
    size_t first_line;
    float first_y = 0;
    float y;
    size_t line;
    size_t selection_start, selection_end;
    HRESULT hr;
    if (!editor_create_device(editor)) return;
    background = editor_color(editor->background_color, 1);
    editor->render_target->lpVtbl->BeginDraw(editor->render_target);
    editor->render_target->lpVtbl->Clear(editor->render_target, &background);
    clip.left = 0;
    clip.top = 0;
    clip.right = (float)editor->width;
    clip.bottom = (float)editor->height;
    editor->render_target->lpVtbl->PushAxisAlignedClip(
        editor->render_target, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    first_line = tinta_editor_document_line_from_y(
        &editor->document,
        fmaxf(0, editor->scroll_y - editor->padding_top), &first_y);
    y = editor->padding_top + first_y - editor->scroll_y;
    editor_selection(editor, &selection_start, &selection_end);
    if (!editor->focused && !(GetWindowLongPtrW(
            editor->hwnd, GWL_STYLE) & ES_NOHIDESEL))
        selection_end = selection_start;
    for (line = first_line;
         line < tinta_editor_document_line_count(&editor->document) &&
         y < editor->height;
         line++) {
        wchar_t *text = NULL;
        size_t length = 0;
        size_t line_start = 0;
        IDWriteTextLayout *layout;
        TintaDWriteTextMetrics metrics;
        TintaEditorLineMetric line_metric;
        D2D1_POINT_2F origin;
        size_t selected_start;
        size_t selected_end;
        layout = editor_create_layout(editor, line, &text, &length,
                                      &line_start, &metrics);
        if (!layout) break;
        tinta_editor_document_get_line(
            &editor->document, line, NULL, &line_metric);
        if (line_metric.generation != editor->layout_generation) {
            tinta_editor_document_set_line_metric(
                &editor->document, line,
                metrics.widthIncludingTrailingWhitespace,
                fmaxf(editor->default_line_height, metrics.height),
                editor->layout_generation);
            line_metric.height = fmaxf(editor->default_line_height,
                                      metrics.height);
        }
        origin.x = editor->padding_left - editor->scroll_x;
        origin.y = y;
        selected_start = selection_start > line_start ?
            selection_start - line_start : 0;
        selected_end = selection_end < line_start + length ?
            selection_end - line_start : length;
        if (selected_start < selected_end)
            editor_draw_selection(editor, layout, origin, selected_start,
                                  selected_end - selected_start, false);
        editor_set_brush(editor, IsWindowEnabled(editor->hwnd) ?
            editor->text_color : editor_colorref(GetSysColor(COLOR_GRAYTEXT)),
            1);
        editor->render_target->lpVtbl->DrawTextLayout(
            editor->render_target, origin, layout,
            (ID2D1Brush *)editor->brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        if (selected_start < selected_end)
            editor_draw_selection(editor, layout, origin, selected_start,
                                  selected_end - selected_start, true);
        y += line_metric.height;
        layout->lpVtbl->Release(layout);
        free(text);
    }
    editor_draw_scrollbars(editor);
    editor->render_target->lpVtbl->PopAxisAlignedClip(editor->render_target);
    hr = editor->render_target->lpVtbl->EndDraw(
        editor->render_target, NULL, NULL);
    if (hr == D2DERR_RECREATE_TARGET) editor_discard_device(editor);
    editor_position_caret(editor);
}

static void editor_show_context_menu(TintaEditor *editor, int x, int y) {
    HMENU menu = CreatePopupMenu();
    UINT command;
    size_t start, end;
    if (!menu) return;
    editor_selection(editor, &start, &end);
    AppendMenuW(menu, MF_STRING |
        (tinta_editor_document_can_undo(&editor->document) && !editor->read_only ?
         MF_ENABLED : MF_GRAYED), 1, L"Undo");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (start != end && !editor->read_only ?
        MF_ENABLED : MF_GRAYED), 2, L"Cut");
    AppendMenuW(menu, MF_STRING | (start != end ? MF_ENABLED : MF_GRAYED),
                3, L"Copy");
    AppendMenuW(menu, MF_STRING | (!editor->read_only &&
        IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_ENABLED : MF_GRAYED),
        4, L"Paste");
    AppendMenuW(menu, MF_STRING | (start != end && !editor->read_only ?
        MF_ENABLED : MF_GRAYED), 5, L"Delete");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, 6, L"Select All");
    if (x == -1 && y == -1) {
        POINT point;
        GetCaretPos(&point);
        ClientToScreen(editor->hwnd, &point);
        x = point.x;
        y = point.y + (int)editor->default_line_height;
    }
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                             x, y, 0, editor->hwnd, NULL);
    DestroyMenu(menu);
    switch (command) {
        case 1: editor_undo(editor); break;
        case 2:
            if (editor_copy_selection(editor))
                editor_replace_selection(editor, L"", 0, true);
            break;
        case 3: editor_copy_selection(editor); break;
        case 4: editor_paste(editor); break;
        case 5: editor_replace_selection(editor, L"", 0, true); break;
        case 6: editor_set_selection(editor, 0, editor_length(editor), true); break;
    }
}

static void editor_position_ime(TintaEditor *editor) {
    HIMC context;
    COMPOSITIONFORM composition;
    CANDIDATEFORM candidate;
    POINT point;
    if (!editor->focused) return;
    editor_position_caret(editor);
    GetCaretPos(&point);
    context = ImmGetContext(editor->hwnd);
    if (!context) return;
    memset(&composition, 0, sizeof(composition));
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = point;
    composition.ptCurrentPos.y += (LONG)editor->default_line_height;
    ImmSetCompositionWindow(context, &composition);
    memset(&candidate, 0, sizeof(candidate));
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = composition.ptCurrentPos;
    ImmSetCandidateWindow(context, &candidate);
    ImmReleaseContext(editor->hwnd, context);
}

static void editor_ime_result(TintaEditor *editor, LPARAM lparam) {
    HIMC context;
    LONG bytes;
    wchar_t *text;
    if (!(lparam & GCS_RESULTSTR) || editor->read_only) return;
    context = ImmGetContext(editor->hwnd);
    if (!context) return;
    bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR, NULL, 0);
    if (bytes > 0) {
        text = (wchar_t *)malloc((size_t)bytes + sizeof(wchar_t));
        if (text) {
            ImmGetCompositionStringW(context, GCS_RESULTSTR, text, bytes);
            text[bytes / sizeof(wchar_t)] = 0;
            editor_replace_selection(editor, text,
                (size_t)bytes / sizeof(wchar_t), true);
            free(text);
        }
    }
    ImmReleaseContext(editor->hwnd, context);
}

static bool editor_assign(TintaEditor *editor,
                          const wchar_t *text, size_t length,
                          bool reject_nul) {
    bool valid;
    size_t normalized = editor_normalized_length(text, length, &valid);
    if (!valid) {
        editor_notify(editor, EN_ERRSPACE);
        return false;
    }
    if (normalized > editor->document.text_limit) {
        editor_notify(editor, EN_MAXTEXT);
        return false;
    }
    if (!tinta_editor_document_assign(&editor->document,
            text, length, reject_nul, editor->default_line_height)) {
        editor_notify(editor, EN_ERRSPACE);
        return false;
    }
    editor->anchor = editor->caret = 0;
    editor->scroll_x = editor->scroll_y = 0;
    editor->layout_cursor = 0;
    editor->layout_start = 0;
    editor->layout_wrapped = false;
    editor_schedule_layout(editor);
    editor_notify(editor, EN_UPDATE);
    editor_notify(editor, EN_CHANGE);
    tinta_editor_uia_raise_text_changed(editor);
    editor_selection_changed(editor, false);
    return true;
}

static bool editor_apply_theme(TintaEditor *editor, int index) {
    const TintaTheme *theme;
    if (index == TINTA_THEME_SYSTEM) {
        editor->use_system_theme = true;
        editor_update_system_colors(editor);
        if (editor_system_log_font(editor, &editor->log_font))
            editor_refresh_owned_font(editor);
        return editor_update_format(editor);
    }
    if (index < 0 || index >= (int)TINTA_THEME_COUNT) return false;
    theme = &TINTA_THEMES[index];
    editor->use_system_theme = false;
    editor->background_color = theme->background;
    editor->text_color = theme->text;
    editor->selection_color = theme->accent;
    editor->selection_text_color = theme->dark ? 0xffffff : 0x000000;
    wcsncpy_s(editor->log_font.lfFaceName, LF_FACESIZE,
              theme->font_family, _TRUNCATE);
    editor_refresh_owned_font(editor);
    editor_update_format(editor);
    return true;
}

static bool editor_apply_custom_theme(TintaEditor *editor,
                                      const TintaThemeSpec *theme) {
    if (!theme || theme->cb_size < sizeof(*theme) || !theme->font_family)
        return false;
    editor->use_system_theme = false;
    editor->background_color = theme->background;
    editor->text_color = theme->text;
    editor->selection_color = theme->accent;
    editor->selection_text_color = theme->dark ? 0xffffff : 0x000000;
    wcsncpy_s(editor->log_font.lfFaceName, LF_FACESIZE,
              theme->font_family, _TRUNCATE);
    editor_refresh_owned_font(editor);
    return editor_update_format(editor);
}

static bool editor_initialize(TintaEditor *editor, HWND hwnd) {
    RECT client;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    memset(editor, 0, sizeof(*editor));
    editor->hwnd = hwnd;
    editor->instance = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    GetClientRect(hwnd, &client);
    editor->width = client.right;
    editor->height = client.bottom;
    editor->dpi_scale = GetDpiForWindow(hwnd) / 96.0f;
    editor->redraw_enabled = true;
    editor->read_only = (style & ES_READONLY) != 0;
    editor->style_no_wrap =
        (style & (WS_HSCROLL | ES_AUTOHSCROLL)) != 0;
    editor->allow_vertical_overlay = (style & WS_VSCROLL) != 0;
    editor->allow_horizontal_overlay = (style & WS_HSCROLL) != 0;
    editor->wrap_mode = TINTA_EDITOR_WRAP_STYLE;
    editor->desired_x = -1;
    editor->layout_generation = 1;
    editor->default_line_height = editor_scale(editor, 20);
    editor->tab_width = editor_scale(editor, 32);
    editor->use_system_theme = true;
    editor_update_system_colors(editor);
    tinta_editor_document_init(&editor->document,
                               editor->default_line_height);
    editor->document.text_limit = 0x7ffffffeu;
    if (!tinta_shared_graphics_acquire(&editor->d2d_factory,
            &editor->dwrite_factory, &editor->font_fallback))
        return false;
    if (!editor_system_log_font(editor, &editor->log_font)) {
        editor->log_font.lfHeight = -16;
        wcscpy_s(editor->log_font.lfFaceName, LF_FACESIZE, L"Segoe UI");
    }
    editor_refresh_owned_font(editor);
    if (!editor_update_format(editor)) return false;
    return true;
}

static void editor_destroy(TintaEditor *editor) {
    if (!editor) return;
    KillTimer(editor->hwnd, TINTA_TIMER_EDITOR_AUTOSCROLL);
    tinta_editor_uia_disconnect(editor);
    if (editor->focused) {
        HideCaret(editor->hwnd);
        DestroyCaret();
    }
    editor_discard_device(editor);
    editor_release_unknown((void **)&editor->format);
    editor_release_unknown((void **)&editor->font_fallback);
    editor_release_unknown((void **)&editor->dwrite_factory);
    editor_release_unknown((void **)&editor->d2d_factory);
    if (editor->own_font && editor->font) DeleteObject(editor->font);
    tinta_editor_document_destroy(&editor->document);
}

static LRESULT editor_dispatch_tem(TintaEditor *editor, UINT message,
                                   WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case TEM_SETTEXTEX: {
            const TintaEditorText *text = (const TintaEditorText *)lparam;
            return text && text->cb_size >= sizeof(*text) && !text->flags &&
                editor_assign(editor, text->text, text->text_length, true);
        }
        case TEM_GETTEXTRANGE: {
            TintaEditorTextRange *range = (TintaEditorTextRange *)lparam;
            size_t length;
            size_t copied;
            if (!range || range->cb_size < sizeof(*range) ||
                range->start > range->end || range->end > editor_length(editor))
                return 0;
            if (!range->text && range->text_capacity) return 0;
            length = range->end - range->start;
            copied = range->text_capacity ?
                (length < range->text_capacity - 1 ? length :
                                                     range->text_capacity - 1) : 0;
            if (copied && range->text)
                tinta_editor_document_copy(&editor->document,
                    range->start, copied, range->text);
            if (range->text && range->text_capacity) range->text[copied] = 0;
            return (LRESULT)length;
        }
        case TEM_SETSELECTION: {
            const TintaEditorSelection *selection =
                (const TintaEditorSelection *)lparam;
            if (!selection || selection->cb_size < sizeof(*selection)) return FALSE;
            editor_set_selection(editor, selection->anchor,
                                 selection->caret, true);
            return TRUE;
        }
        case TEM_GETSELECTION: {
            TintaEditorSelection *selection = (TintaEditorSelection *)lparam;
            if (!selection || selection->cb_size < sizeof(*selection)) return FALSE;
            selection->anchor = editor->anchor;
            selection->caret = editor->caret;
            return TRUE;
        }
        case TEM_SETWORDWRAP:
            if (wparam > TINTA_EDITOR_WRAP_OFF) return FALSE;
            editor->wrap_mode = (TintaEditorWrapMode)wparam;
            editor->scroll_x = 0;
            editor_invalidate_layout(editor);
            return TRUE;
        case TEM_GETWORDWRAP:
            return editor->wrap_mode;
        case TEM_SETUNDOLIMIT:
            if (!lparam) return FALSE;
            tinta_editor_document_set_undo_limit(
                &editor->document, *(const size_t *)lparam);
            return TRUE;
        case TEM_GETUNDOLIMIT:
            if (!lparam) return FALSE;
            *(size_t *)lparam = editor->document.undo_limit;
            return TRUE;
        case TEM_CANREDO:
            return tinta_editor_document_can_redo(&editor->document);
        case TEM_REDO:
            return editor_redo(editor);
        case TEM_SETBUILTINTHEME:
            return editor_apply_theme(editor, (int)wparam);
        case TEM_SETCUSTOMTHEME:
            return editor_apply_custom_theme(
                editor, (const TintaThemeSpec *)lparam);
        case TEM_SETSCROLLPOS: {
            const TintaScrollPosition *position =
                (const TintaScrollPosition *)lparam;
            if (!position || position->cb_size < sizeof(*position) ||
                !isfinite(position->x) || !isfinite(position->y)) return FALSE;
            editor->scroll_x = position->x;
            editor->scroll_y = position->y;
            editor_clamp_scroll(editor);
            InvalidateRect(editor->hwnd, NULL, FALSE);
            editor_position_caret(editor);
            return TRUE;
        }
        case TEM_GETSCROLLPOS: {
            TintaScrollPosition *position = (TintaScrollPosition *)lparam;
            if (!position || position->cb_size < sizeof(*position)) return FALSE;
            position->x = editor->scroll_x;
            position->y = editor->scroll_y;
            return TRUE;
        }
        case TEM_GETCONTENTSIZE: {
            TintaContentSize *size = (TintaContentSize *)lparam;
            if (!size || size->cb_size < sizeof(*size)) return FALSE;
            size->width = editor->document.lines.maximum_width +
                editor->padding_left + editor->padding_right;
            size->height = editor->document.lines.total_height +
                editor->padding_top + editor->padding_bottom;
            return TRUE;
        }
    }
    return 0;
}

static LRESULT editor_get_text(TintaEditor *editor,
                               WPARAM capacity, LPARAM buffer) {
    size_t length = editor_length(editor);
    size_t copied = capacity ? (length < (size_t)capacity - 1 ?
        length : (size_t)capacity - 1) : 0;
    if (buffer && capacity) {
        if (copied)
            tinta_editor_document_copy(&editor->document, 0, copied,
                                        (wchar_t *)buffer);
        ((wchar_t *)buffer)[copied] = 0;
    }
    return (LRESULT)copied;
}

static LRESULT editor_edit_message(TintaEditor *editor, UINT message,
                                   WPARAM wparam, LPARAM lparam) {
    size_t start, end;
    switch (message) {
        case EM_GETSEL:
            editor_selection(editor, &start, &end);
            if (wparam) *(DWORD *)wparam = (DWORD)start;
            if (lparam) *(DWORD *)lparam = (DWORD)end;
            return MAKELRESULT((WORD)start, (WORD)end);
        case EM_SETSEL: {
            size_t selection_start = (size_t)(INT_PTR)wparam;
            size_t selection_end = (size_t)(INT_PTR)lparam;
            if ((INT_PTR)wparam == -1) selection_start = selection_end;
            if ((INT_PTR)lparam == -1) selection_end = editor_length(editor);
            editor_set_selection(editor, selection_start, selection_end, true);
            return 0;
        }
        case EM_REPLACESEL: {
            const wchar_t *text = (const wchar_t *)lparam;
            return editor_replace_selection(editor, text ? text : L"",
                text ? wcslen(text) : 0, wparam != 0);
        }
        case EM_GETLINECOUNT:
            return (LRESULT)tinta_editor_document_line_count(&editor->document);
        case EM_LINEFROMCHAR: {
            size_t position = (INT_PTR)wparam < 0 ? editor->caret : (size_t)wparam;
            return (LRESULT)tinta_editor_document_line_from_position(
                &editor->document, position);
        }
        case EM_LINEINDEX: {
            size_t line = (INT_PTR)wparam < 0 ?
                tinta_editor_document_line_from_position(
                    &editor->document, editor->caret) : (size_t)wparam;
            if (line >= tinta_editor_document_line_count(&editor->document))
                return -1;
            return (LRESULT)tinta_editor_document_line_start(
                &editor->document, line);
        }
        case EM_LINELENGTH: {
            size_t position = (INT_PTR)wparam < 0 ? editor->caret : (size_t)wparam;
            size_t line = tinta_editor_document_line_from_position(
                &editor->document, position);
            TintaEditorLineMetric metric;
            size_t line_start;
            tinta_editor_document_get_line(&editor->document, line,
                                            &line_start, &metric);
            if (metric.length && tinta_editor_document_char_at(
                    &editor->document, line_start + metric.length - 1) == L'\n')
                metric.length--;
            return (LRESULT)metric.length;
        }
        case EM_GETLINE: {
            size_t line = (size_t)wparam;
            wchar_t *buffer = (wchar_t *)lparam;
            WORD capacity;
            size_t line_start;
            TintaEditorLineMetric metric;
            size_t copied;
            if (!buffer || !tinta_editor_document_get_line(
                    &editor->document, line, &line_start, &metric)) return 0;
            capacity = *(WORD *)buffer;
            copied = metric.length < capacity ? metric.length : capacity;
            if (copied && tinta_editor_document_char_at(
                    &editor->document, line_start + copied - 1) == L'\n')
                copied--;
            tinta_editor_document_copy(&editor->document,
                                       line_start, copied, buffer);
            return (LRESULT)copied;
        }
        case EM_GETFIRSTVISIBLELINE:
            return (LRESULT)tinta_editor_document_line_from_y(
                &editor->document, editor->scroll_y, NULL);
        case EM_LINESCROLL:
            if (!editor_wrap_enabled(editor))
                editor->scroll_x += (int)(INT_PTR)wparam *
                                    editor_scale(editor, 8);
            editor->scroll_y += (int)(INT_PTR)lparam *
                                editor->default_line_height;
            editor_clamp_scroll(editor);
            InvalidateRect(editor->hwnd, NULL, FALSE);
            return TRUE;
        case EM_SCROLLCARET:
            editor_reveal_caret(editor);
            return 0;
        case EM_POSFROMCHAR: {
            size_t position = (size_t)wparam;
            POINT point = {0};
            if (!editor_position_to_point(editor, position, &point)) return -1;
            return MAKELRESULT((WORD)point.x, (WORD)point.y);
        }
        case EM_CHARFROMPOS: {
            POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
            size_t position = editor_position_from_point(
                editor, point.x, point.y);
            size_t line = tinta_editor_document_line_from_position(
                &editor->document, position);
            return MAKELRESULT((WORD)position, (WORD)line);
        }
        case EM_SETREADONLY:
        {
            bool old_value = editor->read_only;
            editor->read_only = wparam != 0;
            if (editor->read_only)
                SetWindowLongPtrW(editor->hwnd, GWL_STYLE,
                    GetWindowLongPtrW(editor->hwnd, GWL_STYLE) | ES_READONLY);
            else
                SetWindowLongPtrW(editor->hwnd, GWL_STYLE,
                    GetWindowLongPtrW(editor->hwnd, GWL_STYLE) & ~ES_READONLY);
            if (old_value != editor->read_only)
                tinta_editor_uia_raise_read_only_changed(
                    editor, old_value, editor->read_only);
            return TRUE;
        }
        case EM_CANUNDO:
            return tinta_editor_document_can_undo(&editor->document);
        case EM_EMPTYUNDOBUFFER:
            tinta_editor_document_clear_history(&editor->document);
            return 0;
        case EM_GETMODIFY:
            return editor->document.modified;
        case EM_SETMODIFY:
            editor->document.modified = wparam != 0;
            return 0;
        case EM_SETLIMITTEXT:
            editor->document.text_limit = wparam ? (size_t)wparam : 0x7ffffffeu;
            return 0;
        case EM_GETLIMITTEXT:
            return (LRESULT)editor->document.text_limit;
        case EM_SETMARGINS:
            if (wparam & EC_LEFTMARGIN)
                editor->padding_left = LOWORD(lparam) == EC_USEFONTINFO ?
                    editor_scale(editor, 4) : LOWORD(lparam);
            if (wparam & EC_RIGHTMARGIN)
                editor->padding_right = HIWORD(lparam) == EC_USEFONTINFO ?
                    editor_scale(editor, 4) : HIWORD(lparam);
            editor_invalidate_layout(editor);
            return 0;
        case EM_GETMARGINS:
            return MAKELRESULT((WORD)editor->padding_left,
                               (WORD)editor->padding_right);
        case EM_SETTABSTOPS:
            if (!wparam)
                editor->tab_width = editor_scale(editor, 32);
            else if (wparam == 1 && lparam)
                editor->tab_width = max(editor_scale(editor, 4),
                    *(const int *)lparam * editor_scale(editor, 2));
            else
                return FALSE;
            if (editor->format)
                editor->format->lpVtbl->SetIncrementalTabStop(
                    editor->format, editor->tab_width);
            editor_invalidate_layout(editor);
            return TRUE;
    }
    return 0;
}

static bool editor_is_edit_message(UINT message) {
    switch (message) {
        case EM_GETSEL:
        case EM_SETSEL:
        case EM_REPLACESEL:
        case EM_GETLINECOUNT:
        case EM_GETLINE:
        case EM_LINEINDEX:
        case EM_LINEFROMCHAR:
        case EM_LINELENGTH:
        case EM_GETFIRSTVISIBLELINE:
        case EM_LINESCROLL:
        case EM_SCROLLCARET:
        case EM_POSFROMCHAR:
        case EM_CHARFROMPOS:
        case EM_SETREADONLY:
        case EM_CANUNDO:
        case EM_EMPTYUNDOBUFFER:
        case EM_GETMODIFY:
        case EM_SETMODIFY:
        case EM_SETLIMITTEXT:
        case EM_GETLIMITTEXT:
        case EM_SETMARGINS:
        case EM_GETMARGINS:
        case EM_SETTABSTOPS:
            return true;
        default:
            return false;
    }
}

LRESULT CALLBACK tinta_editor_window_proc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam) {
    TintaEditor *editor = (TintaEditor *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (editor && message >= TEM_FIRST && message <= TEM_LAST)
        return editor_dispatch_tem(editor, message, wparam, lparam);
    if (editor && editor_is_edit_message(message))
        return editor_edit_message(editor, message, wparam, lparam);
    switch (message) {
        case WM_NCCREATE: {
            TintaEditor *created = (TintaEditor *)calloc(1, sizeof(*created));
            if (!created) return FALSE;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)created);
            if (!editor_initialize(created, hwnd)) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                editor_destroy(created);
                free(created);
                return FALSE;
            }
            tinta_control_window_created();
            return TRUE;
        }
        case WM_CREATE: {
            CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
            ShowScrollBar(hwnd, SB_BOTH, FALSE);
            if (editor && create->lpszName && create->lpszName[0])
                editor_assign(editor, create->lpszName,
                              wcslen(create->lpszName), false);
            return 0;
        }
        case WM_NCDESTROY:
            if (editor) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                editor_destroy(editor);
                free(editor);
                tinta_control_window_destroyed();
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        case WM_SETTEXT:
            return editor && editor_assign(editor,
                (const wchar_t *)lparam,
                lparam ? wcslen((const wchar_t *)lparam) : 0, false);
        case WM_GETTEXT:
            return editor ? editor_get_text(editor, wparam, lparam) : 0;
        case WM_GETTEXTLENGTH:
            return editor ? (LRESULT)editor_length(editor) : 0;
        case WM_SETFONT:
            return editor && editor_set_font(editor, (HFONT)wparam,
                                              LOWORD(lparam) != 0);
        case WM_GETFONT:
            return editor ? (LRESULT)editor->font : 0;
        case WM_SETREDRAW:
            if (editor) editor->redraw_enabled = wparam != 0;
            return 0;
        case WM_ENABLE:
            if (editor) InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_SIZE:
            if (editor) {
                editor->width = LOWORD(lparam);
                editor->height = HIWORD(lparam);
                editor_create_device(editor);
                editor_invalidate_layout(editor);
            }
            return 0;
        case WM_DPICHANGED:
        case WM_DPICHANGED_AFTERPARENT:
            if (editor) {
                float old_scale = editor->dpi_scale;
                editor->dpi_scale = GetDpiForWindow(hwnd) / 96.0f;
                if (editor->use_system_theme) {
                    if (editor_system_log_font(editor, &editor->log_font))
                        editor_refresh_owned_font(editor);
                } else if (editor->own_font && old_scale > 0.0f) {
                    editor->log_font.lfHeight = (LONG)lroundf(
                        editor->log_font.lfHeight *
                        editor->dpi_scale / old_scale);
                    editor_refresh_owned_font(editor);
                }
                editor_discard_device(editor);
                editor_update_format(editor);
            }
            return 0;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
        case WM_SETTINGCHANGE:
            if (editor) {
                editor_update_system_colors(editor);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            if (editor && editor->redraw_enabled) editor_render(editor);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SETFOCUS:
            if (editor) {
                editor->focused = true;
                CreateCaret(hwnd, NULL, max(1, (int)editor_scale(editor, 1)),
                            max(1, (int)editor->default_line_height));
                editor_position_caret(editor);
                ShowCaret(hwnd);
                editor_notify(editor, EN_SETFOCUS);
                tinta_editor_uia_raise_focus_changed(editor);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_KILLFOCUS:
            if (editor) {
                editor->focused = false;
                editor->selecting = false;
                editor->line_selecting = false;
                editor->click_count = 0;
                editor->undo_group = 0;
                editor->pending_high_surrogate = false;
                editor_stop_autoscroll(editor);
                HideCaret(hwnd);
                DestroyCaret();
                editor_notify(editor, EN_KILLFOCUS);
                tinta_editor_uia_raise_focus_changed(editor);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS |
                ((GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_WANTRETURN) ?
                 DLGC_WANTALLKEYS : 0);
        case WM_KEYDOWN:
            if (editor) editor_handle_key(editor, wparam);
            return 0;
        case WM_CHAR:
            if (editor) editor_handle_char(editor, (wchar_t)wparam);
            return 0;
        case WM_UNICHAR:
            if (wparam == UNICODE_NOCHAR) return TRUE;
            if (editor) {
                editor->pending_high_surrogate = false;
                if (wparam <= 0xffff)
                    editor_handle_char(editor, (wchar_t)wparam);
                else if (wparam <= 0x10ffff) {
                    uint32_t codepoint = (uint32_t)wparam - 0x10000;
                    wchar_t pair[2] = {
                        (wchar_t)(0xd800 + (codepoint >> 10)),
                        (wchar_t)(0xdc00 + (codepoint & 0x3ff))
                    };
                    editor_replace_selection(editor, pair, 2, true);
                }
            }
            return 0;
        case WM_COPY:
            return editor && editor_copy_selection(editor);
        case WM_CUT:
            if (editor && !editor->read_only && editor_copy_selection(editor))
                return editor_replace_selection(editor, L"", 0, true);
            return FALSE;
        case WM_PASTE:
            return editor && editor_paste(editor);
        case WM_CLEAR:
            return editor && !editor->read_only &&
                editor_replace_selection(editor, L"", 0, true);
        case WM_UNDO:
            return editor && editor_undo(editor);
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (editor) {
                int x = GET_X_LPARAM(lparam);
                int y = GET_Y_LPARAM(lparam);
                size_t position;
                SetFocus(hwnd);
                editor->undo_group = 0;
                editor->pending_high_surrogate = false;
                editor_update_click(editor, message, x, y);
                if (editor_begin_scrollbar(editor, x, y)) return 0;
                position = editor_position_from_point(editor, x, y);
                if (editor->click_count == 3) {
                    size_t start, end;
                    editor_line_range(editor, position, &start, &end);
                    editor->anchor = start;
                    editor->caret = end;
                    editor->line_selection_anchor =
                        tinta_editor_document_line_from_position(
                            &editor->document, position);
                    editor->line_selecting = true;
                    editor->selecting = false;
                } else if (editor->click_count == 2) {
                    editor_select_word(editor, position);
                    editor->selecting = false;
                    editor->line_selecting = false;
                } else {
                    if (shift_pressed())
                        editor->caret = position;
                    else
                        editor->anchor = editor->caret = position;
                    editor->selecting = true;
                    editor->line_selecting = false;
                }
                SetCapture(hwnd);
                editor_selection_changed(editor, true);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (editor) {
                int x = GET_X_LPARAM(lparam);
                int y = GET_Y_LPARAM(lparam);
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
                if (editor->v_scrollbar_dragging || editor->h_scrollbar_dragging)
                    editor_drag_scrollbar(editor, x, y);
                else if (editor->line_selecting || editor->selecting)
                    editor_extend_mouse_selection(editor, x, y);
                else {
                    editor_update_scrollbar_hover(editor, x, y);
                    SetCursor(LoadCursorW(NULL,
                        editor->v_scrollbar_hovered || editor->h_scrollbar_hovered ?
                        IDC_ARROW : IDC_IBEAM));
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            if (editor && !editor->v_scrollbar_dragging &&
                !editor->h_scrollbar_dragging) {
                editor->v_scrollbar_hovered = false;
                editor->h_scrollbar_hovered = false;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (editor) {
                editor->selecting = false;
                editor->line_selecting = false;
                editor->v_scrollbar_dragging = false;
                editor->h_scrollbar_dragging = false;
                editor_stop_autoscroll(editor);
                if (GetCapture() == hwnd) ReleaseCapture();
                editor_selection_changed(editor, false);
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (editor) {
                editor->selecting = false;
                editor->line_selecting = false;
                editor->v_scrollbar_dragging = false;
                editor->h_scrollbar_dragging = false;
                editor_stop_autoscroll(editor);
            }
            return 0;
        case WM_TIMER:
            if (editor && wparam == TINTA_TIMER_EDITOR_AUTOSCROLL) {
                editor_autoscroll(editor);
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            if (editor) {
                float old = editor->scroll_y;
                editor->scroll_y -= GET_WHEEL_DELTA_WPARAM(wparam) /
                    (float)WHEEL_DELTA * editor->default_line_height * 3;
                editor_clamp_scroll(editor);
                if (old != editor->scroll_y) editor_notify(editor, EN_VSCROLL);
                InvalidateRect(hwnd, NULL, FALSE);
                editor_position_caret(editor);
            }
            return 0;
        case WM_MOUSEHWHEEL:
            if (editor && !editor_wrap_enabled(editor)) {
                float old = editor->scroll_x;
                editor->scroll_x += GET_WHEEL_DELTA_WPARAM(wparam) /
                    (float)WHEEL_DELTA * editor_scale(editor, 48);
                editor_clamp_scroll(editor);
                if (old != editor->scroll_x) editor_notify(editor, EN_HSCROLL);
                InvalidateRect(hwnd, NULL, FALSE);
                editor_position_caret(editor);
            }
            return 0;
        case WM_VSCROLL:
            if (editor) editor_scroll_command(editor, false, wparam);
            return 0;
        case WM_HSCROLL:
            if (editor) editor_scroll_command(editor, true, wparam);
            return 0;
        case WM_CONTEXTMENU:
            if (editor) editor_show_context_menu(
                editor, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_IME_STARTCOMPOSITION:
            if (editor) editor_position_ime(editor);
            return DefWindowProcW(hwnd, message, wparam, lparam);
        case WM_IME_COMPOSITION:
            if (editor) {
                editor_position_ime(editor);
                editor_ime_result(editor, lparam);
            }
            if (lparam & GCS_RESULTSTR) {
                lparam &= ~GCS_RESULTSTR;
                if (!lparam) return 0;
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        case WM_GETOBJECT:
            return editor ? tinta_editor_uia_get_object(
                editor, wparam, lparam) : 0;
        case TINTA_WM_EDITOR_LAYOUT:
            if (editor) editor_process_layout(editor);
            return 0;
        case TINTA_WM_EDITOR_HITTEST:
            if (editor && lparam) {
                TintaEditorHitTest *hit = (TintaEditorHitTest *)lparam;
                hit->position = editor_position_from_point(
                    editor, hit->point.x, hit->point.y);
                return TRUE;
            }
            return FALSE;
        case TINTA_WM_EDITOR_POSITION_POINT:
            if (editor && lparam) {
                TintaEditorPositionPoint *position =
                    (TintaEditorPositionPoint *)lparam;
                return editor_position_to_point(
                    editor, position->position, &position->point);
            }
            return FALSE;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

HRESULT tinta_editor_register_class(HINSTANCE instance) {
    WNDCLASSEXW window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW |
                         CS_DBLCLKS | CS_GLOBALCLASS;
    window_class.lpfnWndProc = tinta_editor_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_IBEAM);
    window_class.lpszClassName = TINTA_TEXT_EDITOR_CLASSW;
    if (RegisterClassExW(&window_class)) return S_OK;
    if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        WNDCLASSEXW existing;
        memset(&existing, 0, sizeof(existing));
        existing.cbSize = sizeof(existing);
        if (GetClassInfoExW(NULL, TINTA_TEXT_EDITOR_CLASSW, &existing) &&
            existing.lpfnWndProc == tinta_editor_window_proc)
            return S_FALSE;
    }
    return HRESULT_FROM_WIN32(GetLastError());
}

void tinta_editor_unregister_class(HINSTANCE instance) {
    UnregisterClassW(TINTA_TEXT_EDITOR_CLASSW, instance);
}
