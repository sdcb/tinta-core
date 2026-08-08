#include "internal/control.h"

#include <math.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include <urlmon.h>

static bool path_has_extension(const wchar_t *path, const wchar_t *extension) {
    size_t path_length;
    size_t extension_length;
    if (!path || !extension) return false;
    path_length = wcslen(path);
    extension_length = wcslen(extension);
    return path_length >= extension_length &&
           !_wcsicmp(path + path_length - extension_length, extension);
}

static const wchar_t *document_path(const TintaStr16 *base_uri,
                                    TintaDocumentFormat format,
                                    TintaStr16 *synthetic) {
    const wchar_t *path = base_uri && base_uri->len ? base_uri->data : NULL;
    if (format == TINTA_FORMAT_MERMAID &&
        (!path || !path_has_extension(path, L".mmd"))) {
        if (!tinta_str16_assign(synthetic, L"document.mmd", 12)) return NULL;
        path = synthetic->data;
    } else if (format == TINTA_FORMAT_MARKDOWN &&
               path && path_has_extension(path, L".mmd")) {
        if (!tinta_str16_assign(synthetic, L"document.md", 11)) return NULL;
        path = synthetic->data;
    }
    return path;
}

bool tinta_control_set_base_uri(TintaControl *control,
                                const wchar_t *base_uri) {
    if (!control) return false;
    if (base_uri)
        return tinta_str16_assign(
            &control->base_uri, base_uri, wcslen(base_uri));
    tinta_str16_clear(&control->base_uri);
    return true;
}

void tinta_control_stream_stop(TintaControl *control) {
    if (!control) return;
    KillTimer(control->view.hwnd, TINTA_TIMER_STREAM);
    tinta_stream_async_close(control->stream_async);
    control->stream_async = NULL;
    control->stream_active = false;
    control->stream_dirty = false;
    control->stream_parse_in_flight = false;
    control->stream_layout_in_flight = false;
    control->stream_ending = false;
    control->stream_utf8_expected = 0;
}

static bool validate_stream_chunk(TintaControl *control,
                                  const char *bytes, size_t length,
                                  unsigned int *new_expected,
                                  uint32_t *new_codepoint,
                                  uint32_t *new_minimum,
                                  size_t *new_complete_length) {
    unsigned int expected = control->stream_utf8_expected;
    uint32_t codepoint = control->stream_utf8_codepoint;
    uint32_t minimum = control->stream_utf8_minimum;
    size_t complete = control->stream_complete_length;
    size_t index;
    size_t base = control->stream_buffer.len;
    for (index = 0; index < length; index++) {
        unsigned char value = (unsigned char)bytes[index];
        if (!expected) {
            if (value <= 0x7f) {
                complete = base + index + 1;
            } else if (value >= 0xc2 && value <= 0xdf) {
                expected = 1;
                codepoint = value & 0x1f;
                minimum = 0x80;
            } else if (value >= 0xe0 && value <= 0xef) {
                expected = 2;
                codepoint = value & 0x0f;
                minimum = 0x800;
            } else if (value >= 0xf0 && value <= 0xf4) {
                expected = 3;
                codepoint = value & 0x07;
                minimum = 0x10000;
            } else {
                return false;
            }
        } else {
            if ((value & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (value & 0x3f);
            expected--;
            if (!expected) {
                if (codepoint < minimum || codepoint > 0x10ffff ||
                    (codepoint >= 0xd800 && codepoint <= 0xdfff))
                    return false;
                complete = base + index + 1;
            }
        }
    }
    *new_expected = expected;
    *new_codepoint = codepoint;
    *new_minimum = minimum;
    *new_complete_length = complete;
    return true;
}

static void notify_stream_updated(TintaControl *control) {
    TintaStreamUpdateNotify notice;
    memset(&notice, 0, sizeof(notice));
    notice.hdr.code = TMN_STREAMUPDATED;
    notice.revision = control->stream_displayed_revision;
    notice.utf8_length = control->stream_displayed_length;
    notice.content_size.cb_size = sizeof(notice.content_size);
    notice.content_size.width = control->view.content_width;
    notice.content_size.height = control->view.content_height;
    tinta_control_notify_parent(control, &notice.hdr);
}

static void notify_content_updated(TintaControl *control) {
    TintaContentUpdateNotify notice;
    memset(&notice, 0, sizeof(notice));
    notice.hdr.code = TMN_CONTENTUPDATED;
    notice.flags = TINTA_CONTENT_UPDATE_RESOURCE;
    notice.revision = control->stream_displayed_revision;
    notice.utf8_length = control->stream_displayed_length;
    notice.content_size.cb_size = sizeof(notice.content_size);
    notice.content_size.width = control->view.content_width;
    notice.content_size.height = control->view.content_height;
    tinta_control_notify_parent(control, &notice.hdr);
}

bool tinta_control_apply_auto_size(TintaControl *control) {
    RECT window_rect;
    RECT client_rect;
    TintaAutoSizeNotify notice;
    float target;
    float minimum;
    float maximum;
    int old_client_height;
    int old_window_height;
    int target_client_height;
    int target_window_height;
    int window_width;
    if (!control ||
        !(control->auto_size.flags & TINTA_AUTOSIZE_HEIGHT) ||
        !control->view.layout_complete || !control->view.hwnd)
        return false;
    target = ceilf(control->view.content_height);
    minimum = control->auto_size.min_height * control->view.dpi_scale;
    if (target < minimum) target = minimum;
    if (control->auto_size.flags & TINTA_AUTOSIZE_MAX_HEIGHT) {
        maximum = control->auto_size.max_height * control->view.dpi_scale;
        if (target > maximum) target = maximum;
    }
    target_client_height = max(1, (int)ceilf(target));
    old_client_height = control->view.height;
    if (target_client_height == old_client_height) return false;
    if (!GetWindowRect(control->view.hwnd, &window_rect)) return false;
    old_window_height = window_rect.bottom - window_rect.top;
    window_width = window_rect.right - window_rect.left;
    target_window_height = target_client_height +
        max(0, old_window_height - old_client_height);
    if (!SetWindowPos(control->view.hwnd, NULL, 0, 0,
            window_width, target_window_height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
        return false;
    GetClientRect(control->view.hwnd, &client_rect);
    memset(&notice, 0, sizeof(notice));
    notice.hdr.code = TMN_AUTOSIZED;
    notice.old_client_height = old_client_height;
    notice.new_client_height = client_rect.bottom - client_rect.top;
    notice.new_window_height = target_window_height;
    notice.content_size.cb_size = sizeof(notice.content_size);
    notice.content_size.width = control->view.content_width;
    notice.content_size.height = control->view.content_height;
    tinta_control_notify_parent(control, &notice.hdr);
    return true;
}

bool tinta_control_stream_flush(TintaControl *control, bool force) {
    TintaStr16 synthetic = {0};
    const wchar_t *path;
    ULONGLONG now;
    if (!control || !control->stream_active ||
        control->stream_parse_in_flight || !control->stream_dirty)
        return true;
    now = GetTickCount64();
    if (!force && now < control->stream_next_due) return true;
    path = document_path(&control->base_uri, control->stream_format,
                         &synthetic);
    control->stream_revision++;
    if (!tinta_stream_async_submit(control->stream_async,
            control->stream_revision,
            control->stream_buffer.data ? control->stream_buffer.data : "",
            control->stream_complete_length, path,
            control->view.max_ast_nodes, control->view.max_ast_depth)) {
        control->stream_revision--;
        tinta_str16_destroy(&synthetic);
        tinta_control_notify_error(control, E_OUTOFMEMORY, L"stream-submit",
            L"The streamed Markdown revision could not be queued.");
        return false;
    }
    tinta_str16_destroy(&synthetic);
    control->stream_parse_in_flight = true;
    control->stream_dirty = false;
    control->stream_layout_started = now;
    control->stream_next_due = now + control->stream_refresh_ms;
    return true;
}

void tinta_control_stream_parsed(TintaControl *control) {
    TintaStreamParseResult *result;
    if (!control || !control->stream_active ||
        control->stream_layout_in_flight || !control->stream_async)
        return;
    result = tinta_stream_async_take(control->stream_async);
    if (!result) return;
    control->stream_parse_in_flight = false;
    if (!result->success) {
        tinta_control_notify_error(control, E_FAIL, L"stream-parse",
            L"The streamed Markdown revision could not be parsed.");
        if (control->stream_ending && !control->stream_dirty)
            tinta_control_stream_stop(control);
        else
            tinta_control_stream_flush(control, control->stream_ending);
        tinta_stream_parse_result_destroy(result);
        return;
    }
    tinta_app_commit_prepared_source(&control->view, &result->prepared, false);
    control->stream_layout_length = result->utf8_length;
    control->stream_layout_revision = result->revision;
    control->stream_layout_in_flight = true;
    control->stream_layout_started = GetTickCount64();
    tinta_stream_parse_result_destroy(result);
    if (control->stream_dirty)
        tinta_control_stream_flush(control, control->stream_ending);
}

void tinta_control_layout_completed(TintaControl *control) {
    ULONGLONG now;
    ULONGLONG elapsed;
    if (!control || !control->view.layout_complete) return;
    tinta_control_apply_auto_size(control);
    if (control->stream_active && control->stream_layout_in_flight) {
        now = GetTickCount64();
        elapsed = now - control->stream_layout_started;
        control->stream_displayed_revision = control->stream_layout_revision;
        control->stream_displayed_length = control->stream_layout_length;
        control->stream_layout_in_flight = false;
        if (elapsed > control->stream_refresh_ms)
            control->stream_next_due = now + elapsed;
        if (control->stream_ending && !control->stream_dirty &&
            !control->stream_parse_in_flight &&
            control->stream_displayed_length ==
                control->stream_complete_length) {
            tinta_control_stream_stop(control);
            tinta_control_notify_code(control, TMN_DOCUMENTREADY);
        } else {
            notify_stream_updated(control);
            if (control->stream_dirty)
                tinta_control_stream_flush(control, control->stream_ending);
        }
        tinta_control_stream_parsed(control);
        tinta_uia_raise_text_changed(&control->view);
    } else if (!control->stream_active && !control->ready_notified) {
        control->ready_notified = true;
        tinta_control_notify_code(control, TMN_DOCUMENTREADY);
        tinta_uia_raise_text_changed(&control->view);
    }
    if (control->content_update_pending) {
        control->content_update_pending = false;
        notify_content_updated(control);
    }
}

bool tinta_control_set_document(TintaControl *control,
                                const char *utf8, size_t length,
                                const wchar_t *base_uri,
                                TintaDocumentFormat format,
                                bool replace_base_uri) {
    TintaStr16 proposed_base = {0};
    TintaStr16 synthetic = {0};
    const wchar_t *path;
    bool result;
    if (!control || (!utf8 && length)) return false;
    if (length > control->limits.max_document_bytes) {
        tinta_control_notify_error(control,
            HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), L"set-document",
            L"The Markdown document exceeds the configured limit.");
        return false;
    }
    if (!tinta_str16_assign(&proposed_base,
            replace_base_uri ? (base_uri ? base_uri : L"") :
            (control->base_uri.data ? control->base_uri.data : L""),
            replace_base_uri ? (base_uri ? wcslen(base_uri) : 0) :
            control->base_uri.len))
        return false;
    path = document_path(&proposed_base, format, &synthetic);
    result = tinta_app_load_source(&control->view,
                                   utf8 ? utf8 : "", length, path);
    tinta_str16_destroy(&synthetic);
    if (!result) {
        tinta_str16_destroy(&proposed_base);
        tinta_control_notify_error(control, E_FAIL, L"parse",
            L"The Markdown document could not be parsed.");
        return false;
    }
    tinta_control_stream_stop(control);
    tinta_str16_destroy(&control->base_uri);
    control->base_uri = proposed_base;
    tinta_control_reset_multi_click(control);
    control->ready_notified = false;
    return true;
}

bool tinta_control_set_text(TintaControl *control, const wchar_t *text) {
    TintaStr8 utf8 = {0};
    bool result;
    if (!text) text = L"";
    if (!tinta_utf16_to_utf8(text, wcslen(text), &utf8)) return false;
    result = tinta_control_set_document(control, utf8.data, utf8.len, NULL,
                                        TINTA_FORMAT_MARKDOWN, false);
    tinta_str8_destroy(&utf8);
    return result;
}

size_t tinta_control_get_text(TintaControl *control, wchar_t *buffer,
                              size_t capacity) {
    TintaStr16 wide = {0};
    size_t length = 0;
    if (!control) return 0;
    if (!tinta_utf8_to_utf16(control->view.source.data,
                             control->view.source.len, &wide))
        return 0;
    length = wide.len;
    if (buffer && capacity) {
        size_t copy = length < capacity - 1 ? length : capacity - 1;
        if (copy) memcpy(buffer, wide.data, copy * sizeof(*buffer));
        buffer[copy] = 0;
    }
    tinta_str16_destroy(&wide);
    return length;
}

wchar_t *tinta_control_resolve_link(TintaControl *control, const char *url) {
    TintaStr16 wide = {0};
    wchar_t combined[4096];
    wchar_t directory[MAX_PATH * 4];
    wchar_t *result = NULL;
    if (!url || !tinta_utf8_to_utf16(url, strlen(url), &wide)) return NULL;
    if (!control->base_uri.len || PathIsURLW(wide.data) ||
        PathIsRelativeW(wide.data) == FALSE) {
        result = tinta_wcsdup_n(wide.data, wide.len);
    } else if (PathIsURLW(control->base_uri.data)) {
        DWORD length = _countof(combined);
        if (SUCCEEDED(UrlCombineW(control->base_uri.data, wide.data, combined,
                                  &length, 0)))
            result = tinta_wcsdup_n(combined, wcslen(combined));
    } else if (wcslen(control->base_uri.data) < _countof(directory)) {
        size_t directory_length;
        wcscpy_s(directory, _countof(directory), control->base_uri.data);
        directory_length = wcslen(directory);
        if (directory_length &&
            directory[directory_length - 1] != L'\\' &&
            directory[directory_length - 1] != L'/' &&
            !PathIsDirectoryW(directory))
            PathRemoveFileSpecW(directory);
        if (PathAppendW(directory, wide.data))
            result = tinta_wcsdup_n(directory, wcslen(directory));
    }
    tinta_str16_destroy(&wide);
    return result;
}

bool tinta_control_resolve_image(TintaApp *app, const char *source,
                                 TintaStr16 *resolved, bool *remote,
                                 bool *blocked) {
    TintaControl *control;
    TintaStr16 original = {0};
    wchar_t *initial;
    TintaResourceNotify notice;
    TintaResourceAction action;
    const wchar_t *chosen;
    bool is_remote;
    if (!app || !source || !resolved || !remote || !blocked) return false;
    control = (TintaControl *)app->resource_context;
    if (!control || !tinta_utf8_to_utf16(source, strlen(source), &original))
        return false;
    initial = tinta_control_resolve_link(control, source);
    if (!initial) {
        tinta_str16_destroy(&original);
        return false;
    }
    is_remote = !_wcsnicmp(initial, L"http://", 7) ||
                !_wcsnicmp(initial, L"https://", 8);
    if ((is_remote && !(control->options.flags & TINTA_OPTION_REMOTE_IMAGES)) ||
        (!is_remote && !(control->options.flags & TINTA_OPTION_LOCAL_IMAGES))) {
        *blocked = true;
        free(initial);
        tinta_str16_destroy(&original);
        return true;
    }
    memset(&notice, 0, sizeof(notice));
    notice.hdr.code = TMN_RESOURCEOPENING;
    notice.kind = is_remote ? TINTA_RESOURCE_REMOTE_IMAGE :
                              TINTA_RESOURCE_LOCAL_IMAGE;
    notice.original_uri = original.data;
    notice.resolved_uri = initial;
    action = (TintaResourceAction)tinta_control_notify_parent(
        control, &notice.hdr);
    if (action == TINTA_RESOURCE_BLOCK) {
        *blocked = true;
        free(initial);
        tinta_str16_destroy(&original);
        return true;
    }
    chosen = action == TINTA_RESOURCE_REPLACE && notice.replacement_uri ?
             notice.replacement_uri : initial;
    is_remote = !_wcsnicmp(chosen, L"http://", 7) ||
                !_wcsnicmp(chosen, L"https://", 8);
    if ((is_remote && !(control->options.flags & TINTA_OPTION_REMOTE_IMAGES)) ||
        (!is_remote && !(control->options.flags & TINTA_OPTION_LOCAL_IMAGES))) {
        *blocked = true;
        free(initial);
        tinta_str16_destroy(&original);
        return true;
    }
    if (!tinta_str16_assign(resolved, chosen, wcslen(chosen))) {
        free(initial);
        tinta_str16_destroy(&original);
        return false;
    }
    *remote = !_wcsnicmp(resolved->data, L"http://", 7) ||
              !_wcsnicmp(resolved->data, L"https://", 8);
    *blocked = false;
    free(initial);
    tinta_str16_destroy(&original);
    return true;
}

bool tinta_control_stream_begin(TintaControl *control,
                                const TintaStreamBegin *begin) {
    TintaStr16 proposed_base = {0};
    TintaStr16 synthetic = {0};
    const wchar_t *path;
    UINT interval;
    if (!control || !begin || begin->cb_size < sizeof(*begin) ||
        begin->flags || begin->format < TINTA_FORMAT_AUTO ||
        begin->format > TINTA_FORMAT_MERMAID)
        return false;
    interval = begin->refresh_interval_ms ? begin->refresh_interval_ms : 50;
    if (interval < 20 || interval > 1000) return false;
    if (!tinta_str16_assign(&proposed_base,
            begin->base_uri ? begin->base_uri : L"",
            begin->base_uri ? wcslen(begin->base_uri) : 0))
        return false;
    path = document_path(&proposed_base, begin->format, &synthetic);
    if (!tinta_app_update_source(&control->view, "", 0, path, true)) {
        tinta_str16_destroy(&synthetic);
        tinta_str16_destroy(&proposed_base);
        tinta_control_notify_error(control, E_FAIL, L"stream-begin",
            L"The empty stream document could not be created.");
        return false;
    }
    tinta_str16_destroy(&synthetic);
    tinta_control_stream_stop(control);
    tinta_str8_clear(&control->stream_buffer);
    tinta_str16_destroy(&control->base_uri);
    control->base_uri = proposed_base;
    control->stream_format = begin->format;
    control->stream_refresh_ms = interval;
    control->stream_next_due = GetTickCount64() + interval;
    control->stream_revision = 0;
    control->stream_layout_revision = 0;
    control->stream_displayed_revision = 0;
    control->stream_complete_length = 0;
    control->stream_layout_length = 0;
    control->stream_displayed_length = 0;
    control->stream_utf8_codepoint = 0;
    control->stream_utf8_minimum = 0;
    control->stream_utf8_expected = 0;
    control->stream_dirty = false;
    control->stream_parse_in_flight = false;
    control->stream_layout_in_flight = false;
    control->stream_ending = false;
    control->content_update_pending = false;
    control->stream_async = tinta_stream_async_create(control->view.hwnd);
    if (!control->stream_async) return false;
    control->stream_active = true;
    control->ready_notified = true;
    if (!SetTimer(control->view.hwnd, TINTA_TIMER_STREAM,
                  min(interval, 50u), NULL)) {
        tinta_control_stream_stop(control);
        return false;
    }
    return true;
}

bool tinta_control_stream_append(TintaControl *control,
                                 const TintaStreamChunk *chunk) {
    unsigned int expected;
    uint32_t codepoint;
    uint32_t minimum;
    size_t complete;
    if (!control || !control->stream_active || control->stream_ending ||
        !chunk || chunk->cb_size < sizeof(*chunk) || chunk->flags ||
        (!chunk->utf8 && chunk->utf8_length))
        return false;
    if (chunk->utf8_length > control->limits.max_document_bytes -
                     min(control->stream_buffer.len,
                         control->limits.max_document_bytes)) {
        tinta_control_notify_error(control,
            HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), L"stream-append",
            L"The streamed Markdown document exceeds the configured limit.");
        return false;
    }
    if (!validate_stream_chunk(control,
            chunk->utf8 ? chunk->utf8 : "", chunk->utf8_length,
            &expected, &codepoint, &minimum, &complete)) {
        tinta_control_notify_error(control,
            HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION),
            L"stream-append", L"The stream chunk contains invalid UTF-8.");
        return false;
    }
    if (chunk->utf8_length && !tinta_str8_append(
            &control->stream_buffer, chunk->utf8, chunk->utf8_length)) {
        tinta_control_notify_error(control, E_OUTOFMEMORY, L"stream-append",
            L"The stream chunk could not be copied.");
        return false;
    }
    control->stream_utf8_expected = expected;
    control->stream_utf8_codepoint = codepoint;
    control->stream_utf8_minimum = minimum;
    control->stream_complete_length = complete;
    if (complete > (control->stream_layout_in_flight ?
            control->stream_layout_length : control->stream_displayed_length))
        control->stream_dirty = true;
    return tinta_control_stream_flush(control, false);
}

bool tinta_control_stream_end(TintaControl *control) {
    if (!control || !control->stream_active) return false;
    if (control->stream_utf8_expected) {
        tinta_control_notify_error(control,
            HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION), L"stream-end",
            L"The stream ends with an incomplete UTF-8 character.");
        return false;
    }
    control->stream_ending = true;
    if (control->stream_dirty)
        return tinta_control_stream_flush(control, true);
    if (!control->stream_layout_in_flight &&
        !control->stream_parse_in_flight) {
        tinta_control_stream_stop(control);
        tinta_control_notify_code(control, TMN_DOCUMENTREADY);
    }
    return true;
}

bool tinta_control_stream_cancel(TintaControl *control) {
    TintaStr16 synthetic = {0};
    const wchar_t *path;
    bool result = true;
    if (!control || !control->stream_active) return false;
    if (control->view.source.len != control->stream_displayed_length) {
        path = document_path(&control->base_uri,
                             control->stream_format, &synthetic);
        result = tinta_app_update_source(&control->view,
            control->stream_buffer.data ? control->stream_buffer.data : "",
            control->stream_displayed_length, path, false);
        tinta_str16_destroy(&synthetic);
    }
    tinta_control_stream_stop(control);
    control->ready_notified = true;
    return result;
}
