#include "internal/control.h"

#include <math.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <windowsx.h>
#include <wctype.h>

static bool find_word_character(wchar_t value) {
    return value == L'_' || iswalnum(value) != 0;
}

static float clamp_float(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int selection_character_class(wchar_t value) {
    if ((value >= L'a' && value <= L'z') ||
        (value >= L'A' && value <= L'Z') ||
        (value >= L'0' && value <= L'9') || value == L'_') return 1;
    if ((unsigned int)value > 127 && !iswspace(value)) return 2;
    return 0;
}

static bool click_in_system_bounds(ULONGLONG earlier,
                                   const POINT *earlier_point,
                                   ULONGLONG now, int x, int y) {
    int half_width;
    int half_height;
    if (!earlier || !earlier_point || now < earlier ||
        now - earlier > GetDoubleClickTime())
        return false;
    half_width = max(1, GetSystemMetrics(SM_CXDOUBLECLK) / 2);
    half_height = max(1, GetSystemMetrics(SM_CYDOUBLECLK) / 2);
    return abs(x - earlier_point->x) <= half_width &&
           abs(y - earlier_point->y) <= half_height;
}

static void logical_line_range(const TintaApp *view, size_t position,
                               size_t *start, size_t *end) {
    size_t line_start;
    size_t line_end;
    if (!view || !start || !end) return;
    position = min(position, view->doc_text.len);
    line_start = position;
    while (line_start && view->doc_text.data[line_start - 1] != L'\n')
        line_start--;
    line_end = position;
    while (line_end < view->doc_text.len &&
           view->doc_text.data[line_end] != L'\n')
        line_end++;
    if (line_end < view->doc_text.len) line_end++;
    *start = line_start;
    *end = line_end;
}

void tinta_control_reset_multi_click(TintaControl *control) {
    if (!control) return;
    if (control->line_selecting && GetCapture() == control->view.hwnd)
        ReleaseCapture();
    control->double_click_armed = false;
    control->suppress_next_double_click = false;
    control->line_selecting = false;
}

static bool begin_line_selection(TintaControl *control, int x, int y) {
    ULONGLONG now;
    size_t position;
    size_t start;
    size_t end;
    if (!control || !(control->options.flags & TINTA_OPTION_SELECTION) ||
        !control->double_click_armed)
        return false;
    now = GetTickCount64();
    if (!click_in_system_bounds(control->double_click_tick,
            &control->double_click_point, now, x, y)) {
        control->double_click_armed = false;
        return false;
    }
    control->double_click_armed = false;
    if (!tinta_text_at(&control->view, (float)x, (float)y, NULL))
        return false;
    tinta_hit_test(&control->view, (float)x, (float)y, &position, NULL);
    logical_line_range(&control->view, position, &start, &end);
    control->line_selection_start = start;
    control->line_selection_end = end;
    control->view.selection_anchor = start;
    control->view.selection_focus = end;
    control->view.selecting = false;
    control->line_selecting = true;
    control->line_notified_anchor = start;
    control->line_notified_focus = end;
    control->triple_click_tick = now;
    control->triple_click_point.x = x;
    control->triple_click_point.y = y;
    control->suppress_next_double_click = true;
    SetCapture(control->view.hwnd);
    InvalidateRect(control->view.hwnd, NULL, FALSE);
    tinta_control_notify_code(control, TMN_SELECTIONCHANGED);
    tinta_uia_raise_selection_changed(&control->view);
    return true;
}

static void extend_line_selection(TintaControl *control, int x, int y) {
    size_t position;
    size_t start;
    size_t end;
    if (!control || !control->line_selecting) return;
    tinta_hit_test(&control->view, (float)x, (float)y, &position, NULL);
    logical_line_range(&control->view, position, &start, &end);
    if (end <= control->line_selection_start) {
        control->view.selection_anchor = control->line_selection_end;
        control->view.selection_focus = start;
    } else if (start >= control->line_selection_end) {
        control->view.selection_anchor = control->line_selection_start;
        control->view.selection_focus = end;
    } else {
        control->view.selection_anchor = control->line_selection_start;
        control->view.selection_focus = control->line_selection_end;
    }
    InvalidateRect(control->view.hwnd, NULL, FALSE);
}

static bool find_matches_at(const TintaControl *control, size_t position,
                            const wchar_t *query, size_t query_length) {
    const wchar_t *document = control->view.doc_text.data;
    size_t document_length = control->view.doc_text.len;
    bool matches;
    if (control->find_flags & TINTA_FIND_MATCH_CASE)
        matches = wcsncmp(document + position, query, query_length) == 0;
    else
        matches = _wcsnicmp(document + position, query, query_length) == 0;
    if (!matches || !(control->find_flags & TINTA_FIND_WHOLE_WORD))
        return matches;
    if (position && find_word_character(document[position - 1])) return false;
    if (position + query_length < document_length &&
        find_word_character(document[position + query_length])) return false;
    return true;
}

static void activate_find(TintaControl *control, size_t index) {
    TintaSearchMatch match;
    size_t run_index;
    if (!control || index >= control->view.viewer_search_matches.len) return;
    control->view.viewer_search_index = (int)index;
    match = TINTA_VEC_AT(TintaSearchMatch,
                         control->view.viewer_search_matches, index);
    for (run_index = 0; run_index < control->view.text_runs.len; run_index++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun,
                                          control->view.text_runs, run_index);
        if (match.start < run->doc_start + run->doc_length &&
            match.start + match.length > run->doc_start) {
            if (tinta_expand_run_block(&control->view, run)) {
                if (!tinta_block_animation_active(&control->view))
                    KillTimer(control->view.hwnd,
                              TINTA_TIMER_BLOCK_ANIMATION);
                if (tinta_layout_document(&control->view))
                    activate_find(control, index);
                return;
            }
            {
                float maximum_y = fmaxf(
                    0, control->view.content_height - control->view.height);
                float maximum_x = fmaxf(
                    0, control->view.content_width - control->view.width);
                control->view.scroll_y = clamp_float(
                    run->y - control->view.height * 0.45f, 0, maximum_y);
                if (!tinta_horizontal_region_scroll_run_into_view(
                        &control->view, run))
                    control->view.scroll_x = clamp_float(
                        run->x + run->width * 0.5f -
                        control->view.width * 0.5f, 0, maximum_x);
            }
            break;
        }
    }
    InvalidateRect(control->view.hwnd, NULL, FALSE);
}

bool tinta_control_find(TintaControl *control,
                        const TintaFindRequest *request) {
    size_t position = 0;
    if (!control || !request || request->cb_size < sizeof(*request) ||
        (!request->text && request->text_length))
        return false;
    if ((control->view.layout_dirty || !control->view.layout_complete) &&
        !tinta_layout_document(&control->view))
        return false;
    if (!tinta_str16_assign(&control->view.search_query,
            request->text ? request->text : L"", request->text_length))
        return false;
    control->find_flags = request->flags;
    tinta_vec_clear(&control->view.viewer_search_matches);
    control->view.viewer_search_index = -1;
    if (!request->text_length) {
        InvalidateRect(control->view.hwnd, NULL, FALSE);
        return true;
    }
    while (position + request->text_length <= control->view.doc_text.len) {
        if (find_matches_at(control, position, request->text,
                            request->text_length)) {
            TintaSearchMatch match = {position, request->text_length};
            if (!tinta_vec_push(&control->view.viewer_search_matches,
                                &match))
                return false;
            position += request->text_length;
        } else {
            position++;
        }
    }
    if (control->view.viewer_search_matches.len) activate_find(control, 0);
    else InvalidateRect(control->view.hwnd, NULL, FALSE);
    return true;
}

void tinta_control_find_step(TintaControl *control, int direction) {
    int current;
    int count;
    if (!control || !control->view.viewer_search_matches.len) return;
    count = (int)control->view.viewer_search_matches.len;
    current = control->view.viewer_search_index;
    current += direction;
    if (control->find_flags & TINTA_FIND_WRAP) {
        if (current < 0) current = count - 1;
        if (current >= count) current = 0;
    } else {
        if (current < 0) current = 0;
        if (current >= count) current = count - 1;
    }
    activate_find(control, (size_t)current);
}

void tinta_control_clear_find(TintaControl *control) {
    if (!control) return;
    tinta_str16_clear(&control->view.search_query);
    tinta_vec_clear(&control->view.viewer_search_matches);
    control->view.viewer_search_index = -1;
    InvalidateRect(control->view.hwnd, NULL, FALSE);
}

static bool control_pressed(void) {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

static bool shift_pressed(void) {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

static bool client_animations_enabled(void) {
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
        return true;
    return enabled != FALSE;
}

static void copy_selection(TintaControl *control) {
    if (!control ||
        control->view.selection_anchor == control->view.selection_focus)
        return;
    if (tinta_copy_selection(&control->view))
        tinta_control_notify_code(control, TMN_COPYCOMPLETED);
    else
        tinta_control_notify_error(control, E_FAIL, L"copy",
            L"The selected text could not be copied to the clipboard.");
}

static void begin_selection(TintaControl *control, int x, int y) {
    const char *url = NULL;
    if (!control || !(control->options.flags & TINTA_OPTION_SELECTION)) return;
    tinta_hit_test(&control->view, (float)x, (float)y,
                   &control->view.selection_anchor, &url);
    control->view.selection_focus = control->view.selection_anchor;
    control->line_selecting = false;
    control->view.selecting = true;
    SetCapture(control->view.hwnd);
    InvalidateRect(control->view.hwnd, NULL, FALSE);
}

static void handle_key(TintaControl *control, WPARAM key) {
    TintaApp *view;
    if (!control || !(control->options.flags & TINTA_OPTION_KEYBOARD_NAVIGATION))
        return;
    view = &control->view;
    if (control_pressed()) {
        if (key == 'A') {
            view->selection_anchor = 0;
            view->selection_focus = view->doc_text.len;
            InvalidateRect(view->hwnd, NULL, FALSE);
            tinta_control_notify_code(control, TMN_SELECTIONCHANGED);
            tinta_uia_raise_selection_changed(view);
            return;
        }
        if (key == 'C') {
            copy_selection(control);
            return;
        }
        if (key == 'F') {
            tinta_control_notify_code(control, TMN_REQUESTFIND);
            return;
        }
        if (key == '0' || key == VK_NUMPAD0) {
            tinta_control_set_zoom(control, 1.0f);
            return;
        }
        if (key == VK_OEM_PLUS || key == VK_ADD) {
            tinta_control_set_zoom(control, view->zoom + 0.1f);
            return;
        }
        if (key == VK_OEM_MINUS || key == VK_SUBTRACT) {
            tinta_control_set_zoom(control, view->zoom - 0.1f);
            return;
        }
    }
    switch (key) {
        case VK_DOWN: tinta_scroll(view, 42.0f * view->dpi_scale); break;
        case VK_UP: tinta_scroll(view, -42.0f * view->dpi_scale); break;
        case VK_NEXT:
        case VK_SPACE: tinta_scroll(view, view->height * 0.82f); break;
        case VK_PRIOR: tinta_scroll(view, -view->height * 0.82f); break;
        case VK_HOME:
            view->scroll_y = 0;
            InvalidateRect(view->hwnd, NULL, FALSE);
            break;
        case VK_END: tinta_scroll(view, view->content_height); break;
        default: return;
    }
    tinta_control_notify_code(control, TMN_SCROLLCHANGED);
}

void tinta_control_activate_link(TintaControl *control, const char *url) {
    wchar_t *resolved;
    TintaLinkNotify notice;
    if (!control || !url || !url[0]) return;
    if (url[0] == '#' && tinta_jump_to_internal_link(&control->view, url))
        return;
    resolved = tinta_control_resolve_link(control, url);
    if (!resolved) return;
    memset(&notice, 0, sizeof(notice));
    notice.hdr.code = TMN_LINKACTIVATE;
    notice.uri = resolved;
    if (!tinta_control_notify_parent(control, &notice.hdr) &&
        (control->options.flags & TINTA_OPTION_OPEN_UNHANDLED_LINKS))
        ShellExecuteW(control->view.hwnd, L"open", resolved, NULL, NULL,
                      SW_SHOWNORMAL);
    free(resolved);
}

bool tinta_control_handle_input(TintaControl *control, UINT message,
                                WPARAM wparam, LPARAM lparam,
                                LRESULT *result) {
    HWND hwnd;
    if (!control || !result) return false;
    hwnd = control->view.hwnd;
    switch (message) {
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            if (control_pressed() &&
                (control->options.flags & TINTA_OPTION_MOUSE_ZOOM)) {
                tinta_control_set_zoom(control, control->view.zoom +
                    (delta > 0 ? 0.1f : -0.1f));
            } else if (shift_pressed()) {
                POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(hwnd, &point);
                if (!tinta_horizontal_region_scroll_at(
                        &control->view, point.x, point.y, -delta / 3.0f)) {
                    control->view.scroll_x = fmaxf(
                        0, control->view.scroll_x - delta / 3.0f);
                    InvalidateRect(hwnd, NULL, FALSE);
                    tinta_control_notify_code(control, TMN_SCROLLCHANGED);
                }
            } else {
                tinta_scroll(&control->view, -delta / 3.0f);
                tinta_control_notify_code(control, TMN_SCROLLCHANGED);
            }
            *result = 0;
            return true;
        }
        case WM_MOUSEHWHEEL: {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            ScreenToClient(hwnd, &point);
            if (!tinta_horizontal_region_scroll_at(
                    &control->view, point.x, point.y, delta / 3.0f)) {
                control->view.scroll_x = fmaxf(
                    0, control->view.scroll_x + delta / 3.0f);
                InvalidateRect(hwnd, NULL, FALSE);
                tinta_control_notify_code(control, TMN_SCROLLCHANGED);
            }
            *result = 0;
            return true;
        }
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lparam);
            int y = GET_Y_LPARAM(lparam);
            control->view.mouse_x = x;
            control->view.mouse_y = y;
            SetFocus(hwnd);
            if (tinta_scrollbar_begin_drag(&control->view, x, y)) {
                tinta_control_reset_multi_click(control);
                SetCapture(hwnd);
            } else {
                bool handled = false;
                bool copied = false;
                if ((control->options.flags &
                     TINTA_OPTION_DOCUMENT_COPY_BUTTON) &&
                    tinta_copy_document_at(&control->view, x, y, &copied)) {
                    handled = true;
                    if (copied)
                        tinta_control_notify_code(control, TMN_COPYCOMPLETED);
                    else
                        tinta_control_notify_error(control, E_FAIL,
                            L"copy-document",
                            L"The Markdown document could not be copied to the clipboard.");
                } else if (tinta_copy_mermaid_at(
                        &control->view, x, y, &copied)) {
                    handled = true;
                    if (copied)
                        tinta_control_notify_code(control, TMN_COPYCOMPLETED);
                    else
                        tinta_control_notify_error(control, E_FAIL,
                            L"copy-mermaid",
                            L"The Mermaid source could not be copied to the clipboard.");
                } else if (tinta_copy_code_at(
                        &control->view, x, y, &copied)) {
                    handled = true;
                    if (copied)
                        tinta_control_notify_code(control, TMN_COPYCOMPLETED);
                    else
                        tinta_control_notify_error(control, E_FAIL,
                            L"copy-code",
                            L"The code block could not be copied to the clipboard.");
                } else if (tinta_toggle_collapsible_at(
                        &control->view, x, y,
                        client_animations_enabled())) {
                    handled = true;
                    if (tinta_block_animation_active(&control->view))
                        SetTimer(hwnd, TINTA_TIMER_BLOCK_ANIMATION, 16, NULL);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                if (handled) {
                    tinta_control_reset_multi_click(control);
                } else if (!begin_line_selection(control, x, y)) {
                    control->suppress_next_double_click = false;
                    begin_selection(control, x, y);
                }
            }
            *result = 0;
            return true;
        }
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lparam);
            int y = GET_Y_LPARAM(lparam);
            bool old_over_copy_button = tinta_code_button_at(
                &control->view,
                control->view.mouse_x, control->view.mouse_y);
            bool old_over_document_button =
                (control->options.flags &
                 TINTA_OPTION_DOCUMENT_COPY_BUTTON) &&
                tinta_document_button_at(&control->view,
                    control->view.mouse_x, control->view.mouse_y);
            bool old_over_mermaid_button = tinta_mermaid_button_at(
                &control->view,
                control->view.mouse_x, control->view.mouse_y);
            bool old_over_collapsible_header = tinta_collapsible_header_at(
                &control->view,
                control->view.mouse_x, control->view.mouse_y);
            control->view.mouse_x = x;
            control->view.mouse_y = y;
            if (!control->view.tracking_mouse) {
                TRACKMOUSEEVENT tracking = {
                    sizeof(tracking), TME_LEAVE, hwnd, 0
                };
                TrackMouseEvent(&tracking);
                control->view.tracking_mouse = true;
            }
            if (tinta_scrollbar_drag(&control->view, x, y)) {
                if (control->view.dragging_horizontal_region < 0)
                    tinta_control_notify_code(control, TMN_SCROLLCHANGED);
            } else if (control->line_selecting) {
                extend_line_selection(control, x, y);
            } else if (control->view.selecting) {
                tinta_hit_test(&control->view, (float)x, (float)y,
                               &control->view.selection_focus, NULL);
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                const char *url = NULL;
                int old_code_block = control->view.hovered_code_block;
                int old_mermaid_block = control->view.hovered_mermaid_block;
                bool hover_changed;
                bool over_copy_button;
                bool over_document_button;
                bool over_mermaid_button;
                bool over_collapsible_header;
                bool text = tinta_text_at(&control->view,
                                           (float)x, (float)y, &url);
                hover_changed = tinta_scrollbar_update_hover(
                    &control->view, x, y);
                control->view.hovered_code_block =
                    tinta_code_block_at(&control->view, x, y);
                control->view.hovered_mermaid_block =
                    tinta_mermaid_block_at(&control->view, x, y);
                over_copy_button = control->view.hovered_code_block >= 0 &&
                    tinta_code_button_at(&control->view, x, y);
                over_document_button =
                    (control->options.flags &
                     TINTA_OPTION_DOCUMENT_COPY_BUTTON) &&
                    tinta_document_button_at(&control->view, x, y);
                over_mermaid_button =
                    control->view.hovered_mermaid_block >= 0 &&
                    tinta_mermaid_button_at(&control->view, x, y);
                over_collapsible_header = tinta_collapsible_header_at(
                    &control->view, x, y);
                if (hover_changed ||
                    old_code_block != control->view.hovered_code_block ||
                    old_mermaid_block != control->view.hovered_mermaid_block ||
                    old_over_copy_button != over_copy_button ||
                    old_over_document_button != over_document_button ||
                    old_over_mermaid_button != over_mermaid_button ||
                    old_over_collapsible_header != over_collapsible_header)
                    InvalidateRect(hwnd, NULL, FALSE);
                SetCursor(LoadCursorW(NULL,
                    over_document_button || over_mermaid_button ||
                    over_copy_button || over_collapsible_header ||
                    (url && url[0]) ? IDC_HAND :
                    text ? IDC_IBEAM : IDC_ARROW));
            }
            *result = 0;
            return true;
        }
        case WM_LBUTTONDBLCLK: {
            int x;
            int y;
            ULONGLONG now;
            size_t position = 0;
            size_t start;
            size_t end;
            int character_class = 0;
            if (!(control->options.flags & TINTA_OPTION_SELECTION)) {
                *result = 0;
                return true;
            }
            x = GET_X_LPARAM(lparam);
            y = GET_Y_LPARAM(lparam);
            now = GetTickCount64();
            if (control->suppress_next_double_click &&
                click_in_system_bounds(control->triple_click_tick,
                    &control->triple_click_point, now, x, y)) {
                control->suppress_next_double_click = false;
                control->double_click_armed = false;
                begin_selection(control, x, y);
                *result = 0;
                return true;
            }
            control->suppress_next_double_click = false;
            if (tinta_scrollbar_begin_drag(&control->view, x, y)) {
                tinta_control_reset_multi_click(control);
                SetCapture(hwnd);
                *result = 0;
                return true;
            }
            if (((control->options.flags &
                  TINTA_OPTION_DOCUMENT_COPY_BUTTON) &&
                 tinta_document_button_at(&control->view, x, y)) ||
                tinta_mermaid_button_at(&control->view, x, y) ||
                tinta_code_button_at(&control->view, x, y) ||
                tinta_collapsible_header_at(&control->view, x, y)) {
                tinta_control_reset_multi_click(control);
                *result = 0;
                return true;
            }
            tinta_hit_test(&control->view, (float)x, (float)y,
                           &position, NULL);
            start = end = position;
            if (position < control->view.doc_text.len)
                character_class = selection_character_class(
                    control->view.doc_text.data[position]);
            if (!character_class && position &&
                selection_character_class(
                    control->view.doc_text.data[position - 1])) {
                start = position - 1;
                character_class = selection_character_class(
                    control->view.doc_text.data[start]);
            }
            while (start && selection_character_class(
                    control->view.doc_text.data[start - 1]) == character_class)
                start--;
            while (end < control->view.doc_text.len &&
                   selection_character_class(
                    control->view.doc_text.data[end]) == character_class)
                end++;
            control->view.selection_anchor = start;
            control->view.selection_focus = end;
            control->view.selecting = false;
            control->line_selecting = false;
            control->double_click_tick = now;
            control->double_click_point.x = x;
            control->double_click_point.y = y;
            control->double_click_armed = tinta_text_at(
                &control->view, (float)x, (float)y, NULL);
            InvalidateRect(hwnd, NULL, FALSE);
            tinta_control_notify_code(control, TMN_SELECTIONCHANGED);
            tinta_uia_raise_selection_changed(&control->view);
            *result = 0;
            return true;
        }
        case WM_MOUSELEAVE:
            control->view.tracking_mouse = false;
            control->view.mouse_x = -1;
            control->view.mouse_y = -1;
            control->view.hovered_code_block = -1;
            control->view.hovered_mermaid_block = -1;
            tinta_scrollbar_update_hover(&control->view, -1, -1);
            InvalidateRect(hwnd, NULL, FALSE);
            *result = 0;
            return true;
        case WM_LBUTTONUP:
            if (tinta_scrollbar_end_drag(&control->view,
                    GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))) {
                ReleaseCapture();
                tinta_control_notify_code(control, TMN_SCROLLCHANGED);
                *result = 0;
                return true;
            }
            if (control->line_selecting) {
                extend_line_selection(control,
                    GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                control->line_selecting = false;
                ReleaseCapture();
                if (control->view.selection_anchor !=
                        control->line_notified_anchor ||
                    control->view.selection_focus !=
                        control->line_notified_focus) {
                    tinta_control_notify_code(control, TMN_SELECTIONCHANGED);
                    tinta_uia_raise_selection_changed(&control->view);
                }
                InvalidateRect(hwnd, NULL, FALSE);
                *result = 0;
                return true;
            }
            if (control->view.selecting) {
                const char *url = NULL;
                size_t position;
                tinta_hit_test(&control->view,
                    (float)GET_X_LPARAM(lparam),
                    (float)GET_Y_LPARAM(lparam), &position, &url);
                control->view.selection_focus = position;
                control->view.selecting = false;
                ReleaseCapture();
                if (url && control->view.selection_anchor == position)
                    tinta_control_activate_link(control, url);
                tinta_control_notify_code(control, TMN_SELECTIONCHANGED);
                tinta_uia_raise_selection_changed(&control->view);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            *result = 0;
            return true;
        case WM_KEYDOWN:
            handle_key(control, wparam);
            *result = 0;
            return true;
        case WM_COPY:
            copy_selection(control);
            *result = 0;
            return true;
        case WM_CONTEXTMENU: {
            TintaContextMenuNotify notice;
            const char *url = NULL;
            wchar_t *resolved = NULL;
            memset(&notice, 0, sizeof(notice));
            notice.hdr.code = TMN_CONTEXTMENU;
            notice.screen.x = GET_X_LPARAM(lparam);
            notice.screen.y = GET_Y_LPARAM(lparam);
            notice.has_selection = control->view.selection_anchor !=
                                   control->view.selection_focus;
            if (tinta_text_at(&control->view,
                    (float)control->view.mouse_x,
                    (float)control->view.mouse_y, &url) && url)
                resolved = tinta_control_resolve_link(control, url);
            notice.link_uri = resolved;
            notice.over_code_block = tinta_code_block_at(&control->view,
                control->view.mouse_x, control->view.mouse_y) >= 0;
            tinta_control_notify_parent(control, &notice.hdr);
            free(resolved);
            *result = 0;
            return true;
        }
    }
    return false;
}
