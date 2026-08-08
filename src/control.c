#include "../include/tinta_core.h"
#include "app.h"
#include "features.h"
#include "stream_async.h"
#include "uia_provider.h"

#include <math.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <urlmon.h>
#include <stdlib.h>
#include <string.h>
#include <windowsx.h>
#include <wctype.h>

extern IMAGE_DOS_HEADER __ImageBase;

typedef struct TintaControl {
    LONG references;
    TintaApp view;
    TintaOptions options;
    TintaLimits limits;
    TintaStr16 base_uri;
    TintaTheme custom_theme;
    wchar_t custom_font[LF_FACESIZE];
    wchar_t custom_code_font[LF_FACESIZE];
    DWORD find_flags;
    bool use_system_theme;
    bool redraw_enabled;
    bool ready_notified;
    TintaStr8 stream_buffer;
    TintaDocumentFormat stream_format;
    UINT stream_refresh_ms;
    ULONGLONG stream_next_due;
    ULONGLONG stream_layout_started;
    uint64_t stream_revision;
    uint64_t stream_layout_revision;
    uint64_t stream_displayed_revision;
    size_t stream_complete_length;
    size_t stream_layout_length;
    size_t stream_displayed_length;
    uint32_t stream_utf8_codepoint;
    uint32_t stream_utf8_minimum;
    unsigned int stream_utf8_expected;
    bool stream_active;
    bool stream_dirty;
    bool stream_parse_in_flight;
    bool stream_layout_in_flight;
    bool stream_ending;
    bool content_update_pending;
    TintaAutoSize auto_size;
    TintaPageMargins page_margins;
    unsigned int notification_depth;
    TintaStreamAsync *stream_async;
} TintaControl;

static SRWLOCK g_class_lock = SRWLOCK_INIT;
static LONG g_initialize_count;
static LONG g_live_controls;
static HINSTANCE g_module;

static LRESULT CALLBACK tinta_control_proc(HWND hwnd, UINT message,
                                           WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK tinta_control_proc_impl(HWND hwnd, UINT message,
                                                WPARAM wparam, LPARAM lparam);
static void control_destroy_state(TintaControl *control);
static void control_activate_link(TintaControl *control, const char *url);

static void control_add_ref(TintaControl *control) {
    if (control) InterlockedIncrement(&control->references);
}

static void control_release(TintaControl *control) {
    if (control && !InterlockedDecrement(&control->references)) {
        control_destroy_state(control);
        free(control);
    }
}

static void control_invoke_link(TintaApp *app, const char *url) {
    char *copy;
    if (!app || !url) return;
    copy = tinta_str8_dup(url, strlen(url));
    if (!copy) return;
    if (!PostMessageW(app->hwnd, TINTA_WM_UIA_INVOKE, 0, (LPARAM)copy))
        free(copy);
}

static TintaControl *control_from_window(HWND hwnd) {
    return (TintaControl *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static float clamp_float(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static DWORD default_option_flags(void) {
    DWORD flags = TINTA_OPTION_SELECTION | TINTA_OPTION_KEYBOARD_NAVIGATION |
                  TINTA_OPTION_MOUSE_ZOOM | TINTA_OPTION_CODE_COPY_BUTTON |
                  TINTA_OPTION_OPEN_UNHANDLED_LINKS;
#if TINTA_ENABLE_LOCAL_IMAGES
    flags |= TINTA_OPTION_LOCAL_IMAGES;
#endif
#if TINTA_ENABLE_REMOTE_IMAGES
    flags |= TINTA_OPTION_REMOTE_IMAGES;
#endif
    return flags;
}

static DWORD supported_option_flags(void) {
    DWORD flags = TINTA_OPTION_SELECTION |
                  TINTA_OPTION_KEYBOARD_NAVIGATION |
                  TINTA_OPTION_MOUSE_ZOOM |
                  TINTA_OPTION_CODE_COPY_BUTTON |
                  TINTA_OPTION_DOCUMENT_COPY_BUTTON |
                  TINTA_OPTION_OPEN_UNHANDLED_LINKS;
#if TINTA_ENABLE_LOCAL_IMAGES
    flags |= TINTA_OPTION_LOCAL_IMAGES;
#endif
#if TINTA_ENABLE_REMOTE_IMAGES
    flags |= TINTA_OPTION_REMOTE_IMAGES;
#endif
    return flags;
}

static TintaLimits default_limits(void) {
    TintaLimits limits;
    memset(&limits, 0, sizeof(limits));
    limits.cb_size = sizeof(limits);
    limits.max_document_bytes = 64u * 1024u * 1024u;
    limits.max_ast_nodes = 1000000u;
    limits.max_ast_depth = 256u;
    limits.max_mermaid_nodes = 10000u;
    limits.max_mermaid_edges = 20000u;
    limits.max_image_pixels = 64ull * 1024ull * 1024ull;
    limits.max_remote_image_bytes = 64ull * 1024ull * 1024ull;
    limits.max_image_resources = 512;
    limits.max_concurrent_downloads = 4;
    return limits;
}

static bool system_uses_dark_mode(void) {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &value, &size);
    return status == ERROR_SUCCESS && value == 0;
}

static LRESULT notify_parent(TintaControl *control, NMHDR *header) {
    HWND parent;
    LRESULT result;
    if (!control || !header) return 0;
    parent = GetParent(control->view.hwnd);
    if (!parent) return 0;
    header->hwndFrom = control->view.hwnd;
    header->idFrom = (UINT_PTR)GetDlgCtrlID(control->view.hwnd);
    control->notification_depth++;
    result = SendMessageW(parent, WM_NOTIFY, header->idFrom, (LPARAM)header);
    control->notification_depth--;
    return result;
}

static bool control_reentrant_message_allowed(UINT message) {
    switch (message) {
        case TMM_GETOPTIONS:
        case TMM_GETLIMITS:
        case TMM_GETZOOM:
        case TMM_GETSCROLLPOS:
        case TMM_GETCONTENTSIZE:
        case TMM_GETFINDSTATE:
        case TMM_GETSELECTION:
        case TMM_GETAUTOSIZE:
        case TMM_GETVERSION:
        case TMM_GETCAPABILITIES:
        case TMM_GETSTATS:
        case TMM_GETPAGEMARGINS:
        case WM_GETTEXT:
        case WM_GETTEXTLENGTH:
        case WM_SIZE:
        case WM_NCDESTROY:
            return true;
        default:
            return false;
    }
}

static void notify_code(TintaControl *control, UINT code) {
    NMHDR header;
    memset(&header, 0, sizeof(header));
    header.code = code;
    notify_parent(control, &header);
}

static void notify_error(TintaControl *control, HRESULT error,
                         const wchar_t *operation, const wchar_t *message) {
    TintaErrorNotify notice;
    memset(&notice, 0, sizeof(notice));
    notice.hdr.code = TMN_ERROR;
    notice.error = error;
    notice.operation = operation;
    notice.message = message;
    notify_parent(control, &notice.hdr);
}

static void control_resource_error(TintaApp *app, bool remote,
                                   const wchar_t *resolved_uri,
                                   HRESULT error) {
    TintaControl *control;
    TintaResourceErrorNotify notice;
    if (!app) return;
    control = (TintaControl *)app->resource_context;
    if (!control) return;
    memset(&notice, 0, sizeof(notice));
    notice.hdr.code = TMN_RESOURCEERROR;
    notice.kind = remote ? TINTA_RESOURCE_REMOTE_IMAGE :
                           TINTA_RESOURCE_LOCAL_IMAGE;
    notice.resolved_uri = resolved_uri;
    notice.error = error;
    notify_parent(control, &notice.hdr);
}

static bool control_apply_theme(TintaControl *control, int index) {
    int old_index;
    const TintaTheme *old_theme;
    if (!control || index < 0 || index >= (int)TINTA_THEME_COUNT) return false;
    old_index = control->view.theme_index;
    old_theme = control->view.theme;
    control->view.theme_index = index;
    control->view.theme = &TINTA_THEMES[index];
    if (!tinta_app_update_formats(&control->view)) {
        control->view.theme_index = old_index;
        control->view.theme = old_theme;
        return false;
    }
    tinta_app_discard_device(&control->view);
    control->view.layout_dirty = true;
    InvalidateRect(control->view.hwnd, NULL, FALSE);
    return true;
}

static bool control_refresh_system_theme(TintaControl *control) {
    if (!control || !control->use_system_theme) return false;
    return control_apply_theme(control,
        system_uses_dark_mode() ? TINTA_THEME_MIDNIGHT : TINTA_THEME_PAPER);
}

static bool control_set_custom_theme(TintaControl *control,
                                     const TintaThemeSpec *theme) {
    int old_index;
    const TintaTheme *old_theme;
    if (!control || !theme || theme->cb_size < sizeof(*theme) ||
        !theme->font_family || !theme->code_font_family)
        return false;
    wcsncpy_s(control->custom_font, LF_FACESIZE, theme->font_family, _TRUNCATE);
    wcsncpy_s(control->custom_code_font, LF_FACESIZE,
              theme->code_font_family, _TRUNCATE);
    control->custom_theme.name = L"Custom";
    control->custom_theme.font_family = control->custom_font;
    control->custom_theme.code_font_family = control->custom_code_font;
    control->custom_theme.dark = theme->dark != FALSE;
    control->custom_theme.background = theme->background;
    control->custom_theme.text = theme->text;
    control->custom_theme.heading = theme->heading;
    control->custom_theme.link = theme->link;
    control->custom_theme.code = theme->code;
    control->custom_theme.code_background = theme->code_background;
    control->custom_theme.quote = theme->quote;
    control->custom_theme.accent = theme->accent;
    control->custom_theme.syntax_keyword = theme->syntax_keyword;
    control->custom_theme.syntax_string = theme->syntax_string;
    control->custom_theme.syntax_comment = theme->syntax_comment;
    control->custom_theme.syntax_number = theme->syntax_number;
    control->custom_theme.syntax_function = theme->syntax_function;
    control->custom_theme.syntax_type = theme->syntax_type;
    control->custom_theme.syntax_control = theme->syntax_control;
    old_index = control->view.theme_index;
    old_theme = control->view.theme;
    control->view.theme_index = -1;
    control->view.theme = &control->custom_theme;
    if (!tinta_app_update_formats(&control->view)) {
        control->view.theme_index = old_index;
        control->view.theme = old_theme;
        return false;
    }
    control->use_system_theme = false;
    tinta_app_discard_device(&control->view);
    control->view.layout_dirty = true;
    InvalidateRect(control->view.hwnd, NULL, FALSE);
    return true;
}

static bool control_set_zoom(TintaControl *control, float zoom) {
    TintaApp *view;
    float old_zoom;
    if (!control) return false;
    view = &control->view;
    old_zoom = view->zoom;
    zoom = clamp_float(zoom, 0.5f, 3.0f);
    if (zoom == old_zoom) return true;
    view->zoom = zoom;
    if (!tinta_app_update_formats(view)) {
        view->zoom = old_zoom;
        return false;
    }
    view->layout_dirty = true;
    InvalidateRect(view->hwnd, NULL, FALSE);
    notify_code(control, TMN_ZOOMCHANGED);
    return true;
}

static bool control_store_base_uri(TintaControl *control,
                                   const wchar_t *base_uri) {
    if (!control) return false;
    if (base_uri)
        return tinta_str16_assign(
            &control->base_uri, base_uri, wcslen(base_uri));
    else {
        tinta_str16_clear(&control->base_uri);
        return true;
    }
}

static bool path_has_extension(const wchar_t *path, const wchar_t *extension) {
    size_t path_length;
    size_t extension_length;
    if (!path || !extension) return false;
    path_length = wcslen(path);
    extension_length = wcslen(extension);
    return path_length >= extension_length &&
           !_wcsicmp(path + path_length - extension_length, extension);
}

static const wchar_t *control_document_path(const TintaStr16 *base_uri,
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

static void control_stream_stop(TintaControl *control) {
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

static bool control_validate_stream_chunk(TintaControl *control,
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
    notify_parent(control, &notice.hdr);
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
    notify_parent(control, &notice.hdr);
}

static bool control_apply_auto_size(TintaControl *control) {
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
    notify_parent(control, &notice.hdr);
    return true;
}

static bool control_stream_flush(TintaControl *control, bool force) {
    TintaStr16 synthetic = {0};
    const wchar_t *path;
    ULONGLONG now;
    if (!control || !control->stream_active ||
        control->stream_parse_in_flight || !control->stream_dirty)
        return true;
    now = GetTickCount64();
    if (!force && now < control->stream_next_due) return true;
    path = control_document_path(&control->base_uri, control->stream_format,
                                 &synthetic);
    control->stream_revision++;
    if (!tinta_stream_async_submit(control->stream_async,
            control->stream_revision,
            control->stream_buffer.data ? control->stream_buffer.data : "",
            control->stream_complete_length, path,
            control->view.max_ast_nodes, control->view.max_ast_depth)) {
        control->stream_revision--;
        tinta_str16_destroy(&synthetic);
        notify_error(control, E_OUTOFMEMORY, L"stream-submit",
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

static void control_stream_parsed(TintaControl *control) {
    TintaStreamParseResult *result;
    if (!control || !control->stream_active ||
        control->stream_layout_in_flight || !control->stream_async)
        return;
    result = tinta_stream_async_take(control->stream_async);
    if (!result) return;
    control->stream_parse_in_flight = false;
    if (!result->success) {
        notify_error(control, E_FAIL, L"stream-parse",
                     L"The streamed Markdown revision could not be parsed.");
        if (control->stream_ending && !control->stream_dirty)
            control_stream_stop(control);
        else
            control_stream_flush(control, control->stream_ending);
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
        control_stream_flush(control, control->stream_ending);
}

static void control_layout_completed(TintaControl *control) {
    ULONGLONG now;
    ULONGLONG elapsed;
    if (!control || !control->view.layout_complete) return;
    control_apply_auto_size(control);
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
            control_stream_stop(control);
            notify_code(control, TMN_DOCUMENTREADY);
        } else {
            notify_stream_updated(control);
            if (control->stream_dirty)
                control_stream_flush(control, control->stream_ending);
        }
        control_stream_parsed(control);
        tinta_uia_raise_text_changed(&control->view);
    } else if (!control->stream_active && !control->ready_notified) {
        control->ready_notified = true;
        notify_code(control, TMN_DOCUMENTREADY);
        tinta_uia_raise_text_changed(&control->view);
    }
    if (control->content_update_pending) {
        control->content_update_pending = false;
        notify_content_updated(control);
    }
}

static bool control_set_document(TintaControl *control,
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
        notify_error(control, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
                     L"set-document", L"The Markdown document exceeds the configured limit.");
        return false;
    }
    if (!tinta_str16_assign(&proposed_base,
            replace_base_uri ? (base_uri ? base_uri : L"") :
            (control->base_uri.data ? control->base_uri.data : L""),
            replace_base_uri ? (base_uri ? wcslen(base_uri) : 0) :
            control->base_uri.len))
        return false;
    path = control_document_path(&proposed_base, format, &synthetic);
    result = tinta_app_load_source(&control->view, utf8 ? utf8 : "", length, path);
    tinta_str16_destroy(&synthetic);
    if (!result) {
        tinta_str16_destroy(&proposed_base);
        notify_error(control, E_FAIL, L"parse", L"The Markdown document could not be parsed.");
        return false;
    }
    control_stream_stop(control);
    tinta_str16_destroy(&control->base_uri);
    control->base_uri = proposed_base;
    control->ready_notified = false;
    return true;
}

static bool control_set_text(TintaControl *control, const wchar_t *text) {
    TintaStr8 utf8 = {0};
    bool result;
    if (!text) text = L"";
    if (!tinta_utf16_to_utf8(text, wcslen(text), &utf8)) return false;
    result = control_set_document(control, utf8.data, utf8.len, NULL,
                                  TINTA_FORMAT_MARKDOWN, false);
    tinta_str8_destroy(&utf8);
    return result;
}

static size_t control_get_text(TintaControl *control, wchar_t *buffer,
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

static bool find_word_character(wchar_t value) {
    return value == L'_' || iswalnum(value) != 0;
}

static int selection_character_class(wchar_t value) {
    if ((value >= L'a' && value <= L'z') ||
        (value >= L'A' && value <= L'Z') ||
        (value >= L'0' && value <= L'9') || value == L'_') return 1;
    if ((unsigned int)value > 127 && !iswspace(value)) return 2;
    return 0;
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

static void control_activate_find(TintaControl *control, size_t index) {
    TintaSearchMatch match;
    size_t run_index;
    if (!control || index >= control->view.viewer_search_matches.len) return;
    control->view.viewer_search_index = (int)index;
    match = TINTA_VEC_AT(TintaSearchMatch, control->view.viewer_search_matches, index);
    for (run_index = 0; run_index < control->view.text_runs.len; run_index++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun, control->view.text_runs,
                                          run_index);
        if (match.start < run->doc_start + run->doc_length &&
            match.start + match.length > run->doc_start) {
            float maximum_y = fmaxf(0, control->view.content_height - control->view.height);
            float maximum_x = fmaxf(0, control->view.content_width - control->view.width);
            control->view.scroll_y = clamp_float(
                run->y - control->view.height * 0.45f, 0, maximum_y);
            if (!tinta_horizontal_region_scroll_run_into_view(
                    &control->view, run))
                control->view.scroll_x = clamp_float(
                    run->x + run->width * 0.5f -
                    control->view.width * 0.5f, 0, maximum_x);
            break;
        }
    }
    InvalidateRect(control->view.hwnd, NULL, FALSE);
}

static bool control_find(TintaControl *control, const TintaFindRequest *request) {
    size_t position = 0;
    if (!control || !request || request->cb_size < sizeof(*request) ||
        (!request->text && request->text_length)) return false;
    if ((control->view.layout_dirty || !control->view.layout_complete) &&
        !tinta_layout_document(&control->view)) return false;
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
            if (!tinta_vec_push(&control->view.viewer_search_matches, &match))
                return false;
            position += request->text_length;
        } else {
            position++;
        }
    }
    if (control->view.viewer_search_matches.len)
        control_activate_find(control, 0);
    else
        InvalidateRect(control->view.hwnd, NULL, FALSE);
    return true;
}

static void control_find_step(TintaControl *control, int direction) {
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
    control_activate_find(control, (size_t)current);
}

static void control_clear_find(TintaControl *control) {
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

static void control_copy_selection(TintaControl *control) {
    if (!control ||
        control->view.selection_anchor == control->view.selection_focus)
        return;
    if (tinta_copy_selection(&control->view))
        notify_code(control, TMN_COPYCOMPLETED);
    else
        notify_error(control, E_FAIL, L"copy",
                     L"The selected text could not be copied to the clipboard.");
}

static void control_begin_selection(TintaControl *control, int x, int y) {
    const char *url = NULL;
    if (!control || !(control->options.flags & TINTA_OPTION_SELECTION)) return;
    tinta_hit_test(&control->view, (float)x, (float)y,
                   &control->view.selection_anchor, &url);
    control->view.selection_focus = control->view.selection_anchor;
    control->view.selecting = true;
    SetCapture(control->view.hwnd);
    InvalidateRect(control->view.hwnd, NULL, FALSE);
}

static void control_handle_key(TintaControl *control, WPARAM key) {
    TintaApp *view;
    if (!control || !(control->options.flags & TINTA_OPTION_KEYBOARD_NAVIGATION))
        return;
    view = &control->view;
    if (control_pressed()) {
        if (key == 'A') {
            view->selection_anchor = 0;
            view->selection_focus = view->doc_text.len;
            InvalidateRect(view->hwnd, NULL, FALSE);
            notify_code(control, TMN_SELECTIONCHANGED);
            tinta_uia_raise_selection_changed(view);
            return;
        }
        if (key == 'C') {
            control_copy_selection(control);
            return;
        }
        if (key == 'F') {
            notify_code(control, TMN_REQUESTFIND);
            return;
        }
        if (key == '0' || key == VK_NUMPAD0) {
            control_set_zoom(control, 1.0f);
            return;
        }
        if (key == VK_OEM_PLUS || key == VK_ADD) {
            control_set_zoom(control, view->zoom + 0.1f);
            return;
        }
        if (key == VK_OEM_MINUS || key == VK_SUBTRACT) {
            control_set_zoom(control, view->zoom - 0.1f);
            return;
        }
    }
    switch (key) {
        case VK_DOWN: tinta_scroll(view, 42.0f * view->dpi_scale); break;
        case VK_UP: tinta_scroll(view, -42.0f * view->dpi_scale); break;
        case VK_NEXT: case VK_SPACE: tinta_scroll(view, view->height * 0.82f); break;
        case VK_PRIOR: tinta_scroll(view, -view->height * 0.82f); break;
        case VK_HOME: view->scroll_y = 0; InvalidateRect(view->hwnd, NULL, FALSE); break;
        case VK_END: tinta_scroll(view, view->content_height); break;
        default: return;
    }
    notify_code(control, TMN_SCROLLCHANGED);
}

static wchar_t *control_resolve_link(TintaControl *control, const char *url) {
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

static bool control_resolve_image(TintaApp *app, const char *source,
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
    initial = control_resolve_link(control, source);
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
    action = (TintaResourceAction)notify_parent(control, &notice.hdr);
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

static void control_activate_link(TintaControl *control, const char *url) {
    wchar_t *resolved;
    TintaLinkNotify notice;
    if (!control || !url || !url[0]) return;
    if (url[0] == '#' && tinta_jump_to_internal_link(&control->view, url)) return;
    resolved = control_resolve_link(control, url);
    if (!resolved) return;
    memset(&notice, 0, sizeof(notice));
    notice.hdr.code = TMN_LINKACTIVATE;
    notice.uri = resolved;
    if (!notify_parent(control, &notice.hdr) &&
        (control->options.flags & TINTA_OPTION_OPEN_UNHANDLED_LINKS))
        ShellExecuteW(control->view.hwnd, L"open", resolved, NULL, NULL,
                      SW_SHOWNORMAL);
    free(resolved);
}

static bool copy_heading_string(wchar_t *destination, size_t capacity,
                                const wchar_t *source) {
    size_t length = source ? wcslen(source) : 0;
    if (!destination || !capacity) return length == 0;
    if (length >= capacity) {
        destination[0] = 0;
        return false;
    }
    if (length) memcpy(destination, source, length * sizeof(*destination));
    destination[length] = 0;
    return true;
}

static bool control_stream_begin(TintaControl *control,
                                 const TintaStreamBegin *begin) {
    TintaStr16 proposed_base = {0};
    TintaStr16 synthetic = {0};
    const wchar_t *path;
    UINT interval;
    if (!control || !begin || begin->cb_size < sizeof(*begin) || begin->flags ||
        begin->format < TINTA_FORMAT_AUTO ||
        begin->format > TINTA_FORMAT_MERMAID)
        return false;
    interval = begin->refresh_interval_ms ? begin->refresh_interval_ms : 50;
    if (interval < 20 || interval > 1000) return false;
    if (!tinta_str16_assign(&proposed_base,
            begin->base_uri ? begin->base_uri : L"",
            begin->base_uri ? wcslen(begin->base_uri) : 0))
        return false;
    path = control_document_path(&proposed_base, begin->format, &synthetic);
    if (!tinta_app_update_source(&control->view, "", 0, path, true)) {
        tinta_str16_destroy(&synthetic);
        tinta_str16_destroy(&proposed_base);
        notify_error(control, E_FAIL, L"stream-begin",
                     L"The empty stream document could not be created.");
        return false;
    }
    tinta_str16_destroy(&synthetic);
    control_stream_stop(control);
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
        control_stream_stop(control);
        return false;
    }
    return true;
}

static bool control_stream_append(TintaControl *control,
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
        notify_error(control, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
                     L"stream-append",
                     L"The streamed Markdown document exceeds the configured limit.");
        return false;
    }
    if (!control_validate_stream_chunk(control,
            chunk->utf8 ? chunk->utf8 : "", chunk->utf8_length,
            &expected, &codepoint, &minimum, &complete)) {
        notify_error(control, HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION),
                     L"stream-append", L"The stream chunk contains invalid UTF-8.");
        return false;
    }
    if (chunk->utf8_length && !tinta_str8_append(
            &control->stream_buffer, chunk->utf8, chunk->utf8_length)) {
        notify_error(control, E_OUTOFMEMORY, L"stream-append",
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
    return control_stream_flush(control, false);
}

static bool control_stream_end(TintaControl *control) {
    if (!control || !control->stream_active) return false;
    if (control->stream_utf8_expected) {
        notify_error(control, HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION),
                     L"stream-end",
                     L"The stream ends with an incomplete UTF-8 character.");
        return false;
    }
    control->stream_ending = true;
    if (control->stream_dirty) return control_stream_flush(control, true);
    if (!control->stream_layout_in_flight &&
        !control->stream_parse_in_flight) {
        control_stream_stop(control);
        notify_code(control, TMN_DOCUMENTREADY);
    }
    return true;
}

static bool control_stream_cancel(TintaControl *control) {
    TintaStr16 synthetic = {0};
    const wchar_t *path;
    bool result = true;
    if (!control || !control->stream_active) return false;
    if (control->view.source.len != control->stream_displayed_length) {
        path = control_document_path(&control->base_uri,
                                     control->stream_format, &synthetic);
        result = tinta_app_update_source(&control->view,
            control->stream_buffer.data ? control->stream_buffer.data : "",
            control->stream_displayed_length, path, false);
        tinta_str16_destroy(&synthetic);
    }
    control_stream_stop(control);
    control->ready_notified = true;
    return result;
}

static LRESULT control_custom_message(TintaControl *control, UINT message,
                                      WPARAM wparam, LPARAM lparam) {
    TintaApp *view = &control->view;
    switch (message) {
        case TMM_SETDOCUMENT: {
            const TintaDocument *document = (const TintaDocument *)lparam;
            if (!document || document->cb_size < sizeof(*document) ||
                document->flags || document->format < TINTA_FORMAT_AUTO ||
                document->format > TINTA_FORMAT_MERMAID)
                return FALSE;
            return control_set_document(control, document->utf8,
                document->utf8_length, document->base_uri, document->format,
                true);
        }
        case TMM_SETBASEURI: {
            if (control->stream_active) return FALSE;
            if (!control_store_base_uri(control, (const wchar_t *)lparam))
                return FALSE;
            tinta_app_invalidate_image_requests(view);
            tinta_app_clear_image_resources(view);
            view->layout_dirty = true;
            InvalidateRect(view->hwnd, NULL, FALSE);
            return TRUE;
        }
        case TMM_SETOPTIONS: {
            const TintaOptions *options = (const TintaOptions *)lparam;
            DWORD old_flags;
            if (!options || options->cb_size < sizeof(*options)) return FALSE;
            old_flags = control->options.flags;
            control->options = *options;
            control->options.flags &= supported_option_flags();
            if ((old_flags ^ control->options.flags) &
                (TINTA_OPTION_LOCAL_IMAGES | TINTA_OPTION_REMOTE_IMAGES)) {
                tinta_app_invalidate_image_requests(view);
                tinta_app_clear_image_resources(view);
                view->layout_dirty = true;
                InvalidateRect(view->hwnd, NULL, FALSE);
            }
            if (!(control->options.flags & TINTA_OPTION_SELECTION))
                view->selection_anchor = view->selection_focus = 0;
            if ((old_flags ^ control->options.flags) &
                TINTA_OPTION_CODE_COPY_BUTTON) {
                if (!(control->options.flags & TINTA_OPTION_CODE_COPY_BUTTON)) {
                    if (view->notice_kind == TINTA_NOTICE_COPIED &&
                        view->notice_code_block >= 0) {
                        KillTimer(view->hwnd, TINTA_TIMER_NOTIFICATION);
                        view->notice_kind = TINTA_NOTICE_NONE;
                        view->notice_code_block = -1;
                    }
                }
                view->hovered_code_block =
                    (control->options.flags & TINTA_OPTION_CODE_COPY_BUTTON) ?
                    tinta_code_block_at(view, view->mouse_x, view->mouse_y) : -1;
                InvalidateRect(view->hwnd, NULL, FALSE);
            }
            if ((old_flags ^ control->options.flags) &
                TINTA_OPTION_DOCUMENT_COPY_BUTTON) {
                view->document_copy_button_enabled =
                    (control->options.flags &
                     TINTA_OPTION_DOCUMENT_COPY_BUTTON) != 0;
                if (!view->document_copy_button_enabled &&
                    view->notice_kind == TINTA_NOTICE_COPIED &&
                    view->notice_code_block < 0) {
                    KillTimer(view->hwnd, TINTA_TIMER_NOTIFICATION);
                    view->notice_kind = TINTA_NOTICE_NONE;
                    view->notice_code_block = -1;
                }
                InvalidateRect(view->hwnd, NULL, FALSE);
            }
            return TRUE;
        }
        case TMM_GETOPTIONS: {
            TintaOptions *options = (TintaOptions *)lparam;
            if (!options || options->cb_size < sizeof(*options)) return FALSE;
            *options = control->options;
            return TRUE;
        }
        case TMM_SETLIMITS: {
            const TintaLimits *limits = (const TintaLimits *)lparam;
            if (!limits || limits->cb_size < sizeof(*limits) ||
                !limits->max_document_bytes || !limits->max_ast_nodes ||
                !limits->max_ast_depth || !limits->max_mermaid_nodes ||
                !limits->max_mermaid_edges ||
                !limits->max_image_pixels || !limits->max_image_resources ||
                !limits->max_remote_image_bytes ||
                !limits->max_concurrent_downloads) return FALSE;
            control->limits = *limits;
            view->max_ast_nodes = limits->max_ast_nodes;
            view->max_ast_depth = limits->max_ast_depth;
            view->max_mermaid_nodes = limits->max_mermaid_nodes;
            view->max_mermaid_edges = limits->max_mermaid_edges;
            view->max_image_pixels = limits->max_image_pixels;
            view->max_remote_image_bytes = limits->max_remote_image_bytes;
            view->max_image_resources = limits->max_image_resources;
            view->max_concurrent_downloads = limits->max_concurrent_downloads;
#if TINTA_ENABLE_MERMAID
            tinta_app_clear_mermaid_cache(view);
#endif
            view->layout_dirty = true;
            InvalidateRect(view->hwnd, NULL, FALSE);
            return TRUE;
        }
        case TMM_GETLIMITS: {
            TintaLimits *limits = (TintaLimits *)lparam;
            if (!limits || limits->cb_size < sizeof(*limits)) return FALSE;
            *limits = control->limits;
            return TRUE;
        }
        case TMM_SETBUILTINTHEME:
            if ((int)wparam == TINTA_THEME_SYSTEM) {
                control->use_system_theme = true;
                return control_refresh_system_theme(control);
            }
            control->use_system_theme = false;
            return control_apply_theme(control, (int)wparam);
        case TMM_SETCUSTOMTHEME:
            return control_set_custom_theme(control, (const TintaThemeSpec *)lparam);
        case TMM_SETZOOM:
            return lparam && control_set_zoom(control, *(const float *)lparam);
        case TMM_GETZOOM:
            if (!lparam) return FALSE;
            *(float *)lparam = view->zoom;
            return TRUE;
        case TMM_SETSCROLLPOS: {
            const TintaScrollPosition *position = (const TintaScrollPosition *)lparam;
            if (!position || position->cb_size < sizeof(*position)) return FALSE;
            view->scroll_x = fmaxf(0, position->x);
            view->scroll_y = fmaxf(0, position->y);
            view->scroll_anchor_pending = false;
            InvalidateRect(view->hwnd, NULL, FALSE);
            notify_code(control, TMN_SCROLLCHANGED);
            return TRUE;
        }
        case TMM_GETSCROLLPOS: {
            TintaScrollPosition *position = (TintaScrollPosition *)lparam;
            if (!position || position->cb_size < sizeof(*position)) return FALSE;
            position->x = view->scroll_x;
            position->y = view->scroll_y;
            return TRUE;
        }
        case TMM_GETCONTENTSIZE: {
            TintaContentSize *size = (TintaContentSize *)lparam;
            if (!size || size->cb_size < sizeof(*size)) return FALSE;
            size->width = view->content_width;
            size->height = view->content_height;
            return TRUE;
        }
        case TMM_FIND:
            return control_find(control, (const TintaFindRequest *)lparam);
        case TMM_FINDNEXT: control_find_step(control, 1); return TRUE;
        case TMM_FINDPREVIOUS: control_find_step(control, -1); return TRUE;
        case TMM_CLEARFIND: control_clear_find(control); return TRUE;
        case TMM_GETFINDSTATE: {
            TintaFindState *state = (TintaFindState *)lparam;
            if (!state || state->cb_size < sizeof(*state)) return FALSE;
            state->match_count = view->viewer_search_matches.len;
            state->current_index = view->viewer_search_index >= 0 ?
                (size_t)view->viewer_search_index : SIZE_MAX;
            return TRUE;
        }
        case TMM_GETHEADINGCOUNT:
            if ((view->layout_dirty || !view->layout_complete) &&
                !tinta_layout_document(view)) return 0;
            return (LRESULT)view->headings.len;
        case TMM_GETHEADING: {
            TintaHeadingInfo *info = (TintaHeadingInfo *)lparam;
            TintaHeading *heading;
            if (!info || info->cb_size < sizeof(*info) ||
                info->index >= view->headings.len) return FALSE;
            heading = TINTA_VEC_PTR(TintaHeading, view->headings, info->index);
            info->level = heading->level;
            return copy_heading_string(info->text, info->text_capacity,
                                       heading->text) &&
                   copy_heading_string(info->anchor, info->anchor_capacity,
                                       heading->slug);
        }
        case TMM_SCROLLTOHEADING: {
            size_t index = (size_t)wparam;
            if (index >= view->headings.len) return FALSE;
            view->scroll_y = fmaxf(0,
                TINTA_VEC_AT(TintaHeading, view->headings, index).y -
                20.0f * view->dpi_scale);
            InvalidateRect(view->hwnd, NULL, FALSE);
            notify_code(control, TMN_SCROLLCHANGED);
            return TRUE;
        }
        case TMM_SELECTALL:
            view->selection_anchor = 0;
            view->selection_focus = view->doc_text.len;
            InvalidateRect(view->hwnd, NULL, FALSE);
            notify_code(control, TMN_SELECTIONCHANGED);
            tinta_uia_raise_selection_changed(view);
            return TRUE;
        case TMM_CLEARSELECTION:
            view->selection_anchor = view->selection_focus = 0;
            InvalidateRect(view->hwnd, NULL, FALSE);
            notify_code(control, TMN_SELECTIONCHANGED);
            tinta_uia_raise_selection_changed(view);
            return TRUE;
        case TMM_GETSELECTION: {
            TintaSelection *selection = (TintaSelection *)lparam;
            if (!selection || selection->cb_size < sizeof(*selection)) return FALSE;
            selection->start = min(view->selection_anchor, view->selection_focus);
            selection->end = max(view->selection_anchor, view->selection_focus);
            return TRUE;
        }
        case TMM_REFRESHAPPEARANCE:
            return control_refresh_system_theme(control);
        case TMM_STREAM_BEGIN:
            return control_stream_begin(control,
                (const TintaStreamBegin *)lparam);
        case TMM_STREAM_APPEND:
            return control_stream_append(control,
                (const TintaStreamChunk *)lparam);
        case TMM_STREAM_END:
            return control_stream_end(control);
        case TMM_STREAM_CANCEL:
            return control_stream_cancel(control);
        case TMM_SETAUTOSIZE: {
            const TintaAutoSize *auto_size =
                (const TintaAutoSize *)lparam;
            DWORD known_flags = TINTA_AUTOSIZE_HEIGHT |
                                TINTA_AUTOSIZE_MAX_HEIGHT;
            if (!auto_size || auto_size->cb_size < sizeof(*auto_size) ||
                (auto_size->flags & ~known_flags) ||
                ((auto_size->flags & TINTA_AUTOSIZE_MAX_HEIGHT) &&
                 (!(auto_size->flags & TINTA_AUTOSIZE_HEIGHT) ||
                  auto_size->max_height <= 0)) ||
                auto_size->min_height < 0 ||
                ((auto_size->flags & TINTA_AUTOSIZE_MAX_HEIGHT) &&
                 auto_size->max_height < auto_size->min_height))
                return FALSE;
            control->auto_size = *auto_size;
            control->auto_size.cb_size = sizeof(control->auto_size);
            control_apply_auto_size(control);
            return TRUE;
        }
        case TMM_GETAUTOSIZE: {
            TintaAutoSize *auto_size = (TintaAutoSize *)lparam;
            if (!auto_size || auto_size->cb_size < sizeof(*auto_size))
                return FALSE;
            *auto_size = control->auto_size;
            return TRUE;
        }
        case TMM_GETVERSION: {
            TintaVersionInfo *version = (TintaVersionInfo *)lparam;
            if (!version || version->cb_size < sizeof(*version)) return FALSE;
            version->cb_size = sizeof(*version);
            version->major = TINTA_CORE_VERSION_MAJOR;
            version->minor = TINTA_CORE_VERSION_MINOR;
            version->patch = TINTA_CORE_VERSION_PATCH;
            return TRUE;
        }
        case TMM_GETCAPABILITIES: {
            TintaCapabilities *capabilities = (TintaCapabilities *)lparam;
            DWORD flags = TINTA_CAPABILITY_STREAMING;
            if (!capabilities || capabilities->cb_size < sizeof(*capabilities))
                return FALSE;
#if TINTA_ENABLE_UIA
            flags |= TINTA_CAPABILITY_UIA;
#endif
#if TINTA_ENABLE_MERMAID
            flags |= TINTA_CAPABILITY_MERMAID;
#endif
#if TINTA_ENABLE_SYNTAX
            flags |= TINTA_CAPABILITY_SYNTAX;
#endif
#if TINTA_ENABLE_REMOTE_IMAGES
            flags |= TINTA_CAPABILITY_REMOTE_IMAGES;
#endif
#if TINTA_ENABLE_LOCAL_IMAGES
            flags |= TINTA_CAPABILITY_LOCAL_IMAGES;
#endif
            capabilities->cb_size = sizeof(*capabilities);
            capabilities->flags = flags;
            capabilities->option_flags = supported_option_flags();
            return TRUE;
        }
        case TMM_GETSTATS: {
            TintaStats *stats = (TintaStats *)lparam;
            if (!stats || stats->cb_size < sizeof(*stats)) return FALSE;
            stats->cb_size = sizeof(*stats);
            stats->document_revision = view->document_revision;
            stats->source_bytes = view->source.len;
            stats->ast_nodes = view->ast_node_count;
            stats->text_runs = view->text_runs.len;
            stats->image_resources = view->image_resources.len;
            stats->parse_time_us = view->parse_time_us;
            stats->layout_time_us = view->layout_time_us;
            stats->draw_calls = view->draw_calls;
            return TRUE;
        }
        case TMM_SETPAGEMARGINS: {
            const TintaPageMargins *margins =
                (const TintaPageMargins *)lparam;
            if (!margins || margins->cb_size < sizeof(*margins) ||
                !isfinite(margins->left) || margins->left < 0 ||
                !isfinite(margins->top) || margins->top < 0 ||
                !isfinite(margins->right) || margins->right < 0 ||
                !isfinite(margins->bottom) || margins->bottom < 0)
                return FALSE;
            control->page_margins = *margins;
            view->page_margin_left = margins->left;
            view->page_margin_top = margins->top;
            view->page_margin_right = margins->right;
            view->page_margin_bottom = margins->bottom;
            view->layout_dirty = true;
            InvalidateRect(view->hwnd, NULL, FALSE);
            return TRUE;
        }
        case TMM_GETPAGEMARGINS: {
            TintaPageMargins *margins = (TintaPageMargins *)lparam;
            if (!margins || margins->cb_size < sizeof(*margins)) return FALSE;
            *margins = control->page_margins;
            return TRUE;
        }
    }
    return 0;
}

static bool control_initialize_state(TintaControl *control, HWND hwnd) {
    TintaSettings settings;
    RECT client;
    memset(&settings, 0, sizeof(settings));
    settings.width = 1;
    settings.height = 1;
    settings.theme_index = system_uses_dark_mode() ?
        TINTA_THEME_MIDNIGHT : TINTA_THEME_PAPER;
    settings.zoom = 1.0f;
    if (!tinta_app_init(&control->view, g_module, &settings)) return false;
    control->view.hwnd = hwnd;
    GetClientRect(hwnd, &client);
    control->view.width = client.right - client.left;
    control->view.height = client.bottom - client.top;
    control->view.dpi_scale = GetDpiForWindow(hwnd) / 96.0f;
    control->options.cb_size = sizeof(control->options);
    control->options.flags = default_option_flags();
    control->view.document_copy_button_enabled =
        (control->options.flags & TINTA_OPTION_DOCUMENT_COPY_BUTTON) != 0;
    control->limits = default_limits();
    control->view.max_ast_nodes = control->limits.max_ast_nodes;
    control->view.max_ast_depth = control->limits.max_ast_depth;
    control->view.max_mermaid_nodes = control->limits.max_mermaid_nodes;
    control->view.max_mermaid_edges = control->limits.max_mermaid_edges;
    control->view.max_image_pixels = control->limits.max_image_pixels;
    control->view.max_remote_image_bytes =
        control->limits.max_remote_image_bytes;
    control->view.max_image_resources = control->limits.max_image_resources;
    control->view.max_concurrent_downloads =
        control->limits.max_concurrent_downloads;
    control->view.resolve_image = control_resolve_image;
    control->view.invoke_link = control_invoke_link;
    control->view.resource_error = control_resource_error;
    control->view.resource_context = control;
    control->use_system_theme = true;
    control->redraw_enabled = true;
    control->auto_size.cb_size = sizeof(control->auto_size);
    control->page_margins.cb_size = sizeof(control->page_margins);
    control->page_margins.left = 40.0f;
    control->page_margins.top = 20.0f;
    control->page_margins.right = 40.0f;
    control->page_margins.bottom = 40.0f;
    control->view.page_margin_left = control->page_margins.left;
    control->view.page_margin_top = control->page_margins.top;
    control->view.page_margin_right = control->page_margins.right;
    control->view.page_margin_bottom = control->page_margins.bottom;
    tinta_str16_init(&control->base_uri);
    tinta_str8_init(&control->stream_buffer);
    return tinta_app_update_formats(&control->view);
}

static void control_destroy_state(TintaControl *control) {
    if (!control) return;
    KillTimer(control->view.hwnd, TINTA_TIMER_STREAM);
    tinta_stream_async_close(control->stream_async);
    control->stream_async = NULL;
    tinta_str8_destroy(&control->stream_buffer);
    tinta_str16_destroy(&control->base_uri);
    tinta_app_destroy(&control->view);
}

static LRESULT CALLBACK tinta_control_proc_impl(HWND hwnd, UINT message,
                                                WPARAM wparam, LPARAM lparam) {
    TintaControl *control = control_from_window(hwnd);
    if (control && control->notification_depth &&
        !control_reentrant_message_allowed(message))
        return FALSE;
    if (message >= TMM_FIRST && message <= TMM_GETPAGEMARGINS && control)
        return control_custom_message(control, message, wparam, lparam);
    switch (message) {
        case WM_NCCREATE: {
            TintaControl *created = (TintaControl *)calloc(1, sizeof(*created));
            if (!created) return FALSE;
            created->references = 1;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)created);
            if (!control_initialize_state(created, hwnd)) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                control_destroy_state(created);
                free(created);
                return FALSE;
            }
            InterlockedIncrement(&g_live_controls);
            return TRUE;
        }
        case WM_CREATE: {
            CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
            if (control && create->lpszName && create->lpszName[0])
                control_set_text(control, create->lpszName);
            return 0;
        }
        case WM_SETTEXT:
            return control && control_set_text(control, (const wchar_t *)lparam);
        case WM_GETTEXT:
            return control ? (LRESULT)min((size_t)wparam - ((size_t)wparam ? 1 : 0),
                control_get_text(control, (wchar_t *)lparam, (size_t)wparam)) : 0;
        case WM_GETTEXTLENGTH:
            return control ? (LRESULT)control_get_text(control, NULL, 0) : 0;
        case WM_SIZE:
            if (control) {
                int old_width = control->view.width;
                control->view.width = LOWORD(lparam);
                control->view.height = HIWORD(lparam);
                tinta_app_create_device(&control->view);
                if (control->view.width != old_width)
                    control->view.layout_dirty = true;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_DPICHANGED:
        case WM_DPICHANGED_AFTERPARENT:
            if (control) {
                control->view.dpi_scale = GetDpiForWindow(hwnd) / 96.0f;
                tinta_app_update_formats(&control->view);
                tinta_app_discard_device(&control->view);
                control->view.layout_dirty = true;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            if (control) control_refresh_system_theme(control);
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            if (control && control->redraw_enabled) {
                tinta_render(&control->view);
                control_layout_completed(control);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SETREDRAW:
            if (control) control->redraw_enabled = wparam != 0;
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_GETOBJECT:
            return control ? tinta_uia_get_object(&control->view, wparam, lparam) : 0;
        case WM_MOUSEWHEEL:
            if (control) {
                int delta = GET_WHEEL_DELTA_WPARAM(wparam);
                if (control_pressed() &&
                    (control->options.flags & TINTA_OPTION_MOUSE_ZOOM)) {
                    control_set_zoom(control, control->view.zoom +
                        (delta > 0 ? 0.1f : -0.1f));
                } else if (shift_pressed()) {
                    POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                    ScreenToClient(hwnd, &point);
                    if (!tinta_horizontal_region_scroll_at(
                            &control->view, point.x, point.y, -delta / 3.0f)) {
                        control->view.scroll_x = fmaxf(
                            0, control->view.scroll_x - delta / 3.0f);
                        InvalidateRect(hwnd, NULL, FALSE);
                        notify_code(control, TMN_SCROLLCHANGED);
                    }
                } else {
                    tinta_scroll(&control->view, -delta / 3.0f);
                    notify_code(control, TMN_SCROLLCHANGED);
                }
            }
            return 0;
        case WM_MOUSEHWHEEL:
            if (control) {
                POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                int delta = GET_WHEEL_DELTA_WPARAM(wparam);
                ScreenToClient(hwnd, &point);
                if (!tinta_horizontal_region_scroll_at(
                        &control->view, point.x, point.y, delta / 3.0f)) {
                    control->view.scroll_x = fmaxf(
                        0, control->view.scroll_x + delta / 3.0f);
                    InvalidateRect(hwnd, NULL, FALSE);
                    notify_code(control, TMN_SCROLLCHANGED);
                }
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (control) {
                int x = GET_X_LPARAM(lparam);
                int y = GET_Y_LPARAM(lparam);
                control->view.mouse_x = x;
                control->view.mouse_y = y;
                SetFocus(hwnd);
                if (tinta_scrollbar_begin_drag(&control->view, x, y)) {
                    SetCapture(hwnd);
                } else {
                    bool handled = false;
                    bool copied = false;
                    if ((control->options.flags &
                         TINTA_OPTION_DOCUMENT_COPY_BUTTON) &&
                        tinta_copy_document_at(&control->view, x, y, &copied)) {
                        handled = true;
                        if (copied)
                            notify_code(control, TMN_COPYCOMPLETED);
                        else
                            notify_error(control, E_FAIL, L"copy-document",
                                L"The Markdown document could not be copied to the clipboard.");
                    } else if ((control->options.flags &
                                TINTA_OPTION_CODE_COPY_BUTTON) &&
                        tinta_copy_code_at(&control->view, x, y, &copied)) {
                        handled = true;
                        if (copied)
                            notify_code(control, TMN_COPYCOMPLETED);
                        else
                            notify_error(control, E_FAIL, L"copy-code",
                                L"The code block could not be copied to the clipboard.");
                    }
                    if (!handled) control_begin_selection(control, x, y);
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (control) {
                int x = GET_X_LPARAM(lparam);
                int y = GET_Y_LPARAM(lparam);
                bool old_over_copy_button =
                    (control->options.flags & TINTA_OPTION_CODE_COPY_BUTTON) &&
                    tinta_code_button_at(&control->view,
                        control->view.mouse_x, control->view.mouse_y);
                bool old_over_document_button =
                    (control->options.flags &
                     TINTA_OPTION_DOCUMENT_COPY_BUTTON) &&
                    tinta_document_button_at(&control->view,
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
                    notify_code(control, TMN_SCROLLCHANGED);
                } else if (control->view.selecting) {
                    tinta_hit_test(&control->view, (float)x, (float)y,
                                   &control->view.selection_focus, NULL);
                    InvalidateRect(hwnd, NULL, FALSE);
                } else {
                    const char *url = NULL;
                    int old_code_block = control->view.hovered_code_block;
                    bool hover_changed;
                    bool over_copy_button;
                    bool over_document_button;
                    bool text = tinta_text_at(&control->view, (float)x, (float)y,
                                              &url);
                    hover_changed = tinta_scrollbar_update_hover(
                        &control->view, x, y);
                    control->view.hovered_code_block =
                        (control->options.flags & TINTA_OPTION_CODE_COPY_BUTTON) ?
                        tinta_code_block_at(&control->view, x, y) : -1;
                    over_copy_button =
                        control->view.hovered_code_block >= 0 &&
                        tinta_code_button_at(&control->view, x, y);
                    over_document_button =
                        (control->options.flags &
                         TINTA_OPTION_DOCUMENT_COPY_BUTTON) &&
                        tinta_document_button_at(&control->view, x, y);
                    if (hover_changed || old_code_block !=
                        control->view.hovered_code_block ||
                        old_over_copy_button != over_copy_button ||
                        old_over_document_button != over_document_button)
                        InvalidateRect(hwnd, NULL, FALSE);
                    SetCursor(LoadCursorW(NULL,
                        over_document_button || over_copy_button ||
                        (url && url[0]) ? IDC_HAND :
                        text ? IDC_IBEAM : IDC_ARROW));
                }
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            if (control && (control->options.flags & TINTA_OPTION_SELECTION)) {
                size_t position = 0;
                size_t start;
                size_t end;
                int character_class = 0;
                tinta_hit_test(&control->view, (float)GET_X_LPARAM(lparam),
                    (float)GET_Y_LPARAM(lparam), &position, NULL);
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
                InvalidateRect(hwnd, NULL, FALSE);
                notify_code(control, TMN_SELECTIONCHANGED);
                tinta_uia_raise_selection_changed(&control->view);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (control) {
                control->view.tracking_mouse = false;
                control->view.mouse_x = -1;
                control->view.mouse_y = -1;
                control->view.hovered_code_block = -1;
                tinta_scrollbar_update_hover(&control->view, -1, -1);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (control && tinta_scrollbar_end_drag(&control->view,
                    GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))) {
                ReleaseCapture();
                notify_code(control, TMN_SCROLLCHANGED);
                return 0;
            }
            if (control && control->view.selecting) {
                const char *url = NULL;
                size_t position;
                tinta_hit_test(&control->view, (float)GET_X_LPARAM(lparam),
                    (float)GET_Y_LPARAM(lparam), &position, &url);
                control->view.selection_focus = position;
                control->view.selecting = false;
                ReleaseCapture();
                if (url && control->view.selection_anchor == position)
                    control_activate_link(control, url);
                notify_code(control, TMN_SELECTIONCHANGED);
                tinta_uia_raise_selection_changed(&control->view);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_KEYDOWN:
            if (control) control_handle_key(control, wparam);
            return 0;
        case WM_COPY:
            if (control) control_copy_selection(control);
            return 0;
        case WM_CONTEXTMENU:
            if (control) {
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
                    resolved = control_resolve_link(control, url);
                notice.link_uri = resolved;
                notice.over_code_block = tinta_code_block_at(&control->view,
                    control->view.mouse_x, control->view.mouse_y) >= 0;
                notify_parent(control, &notice.hdr);
                free(resolved);
            }
            return 0;
        case TINTA_WM_LAYOUT_CHUNK:
            if (control) {
                control->view.layout_chunk_posted = false;
                if (tinta_layout_continue(&control->view)) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (!control->view.layout_complete &&
                        !control->view.layout_dirty) {
                        control->view.layout_chunk_posted =
                            PostMessageW(hwnd, TINTA_WM_LAYOUT_CHUNK, 0, 0) != FALSE;
                    } else if (control->view.layout_complete)
                        control_layout_completed(control);
                }
            }
            return 0;
        case TINTA_WM_IMAGE_READY:
            if (control && tinta_remote_image_complete(&control->view))
                control->content_update_pending = true;
            return 0;
        case TINTA_WM_STREAM_PARSED:
            control_stream_parsed(control);
            return 0;
        case TINTA_WM_UIA_INVOKE:
            if (control && lparam) control_activate_link(control, (const char *)lparam);
            free((void *)lparam);
            return 0;
        case WM_TIMER:
            if (control && wparam == TINTA_TIMER_STREAM) {
                control_stream_flush(control, false);
                return 0;
            }
            if (control && wparam == TINTA_TIMER_NOTIFICATION) {
                KillTimer(hwnd, TINTA_TIMER_NOTIFICATION);
                control->view.notice_kind = TINTA_NOTICE_NONE;
                control->view.notice_code_block = -1;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;
        case WM_NCDESTROY:
            if (control) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                tinta_uia_disconnect(&control->view);
                if (!InterlockedDecrement(&g_live_controls)) {
                    AcquireSRWLockExclusive(&g_class_lock);
                    if (!g_initialize_count && g_module) {
                        UnregisterClassW(TINTA_MARKDOWN_VIEW_CLASSW, g_module);
                        g_module = NULL;
                        tinta_shared_graphics_uninitialize();
                    }
                    ReleaseSRWLockExclusive(&g_class_lock);
                }
                control_release(control);
            }
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK tinta_control_proc(HWND hwnd, UINT message,
                                           WPARAM wparam, LPARAM lparam) {
    TintaControl *control = control_from_window(hwnd);
    LRESULT result;
    void *uia_lock;
    if (!control || message == WM_NCCREATE)
        return tinta_control_proc_impl(hwnd, message, wparam, lparam);
    control_add_ref(control);
    uia_lock = tinta_uia_lock_app(&control->view);
    result = tinta_control_proc_impl(hwnd, message, wparam, lparam);
    tinta_uia_unlock_app(uia_lock);
    control_release(control);
    return result;
}

HRESULT TintaCoreInitialize(void) {
    WNDCLASSEXW window_class;
    HRESULT result = S_OK;
    AcquireSRWLockExclusive(&g_class_lock);
    if (g_initialize_count > 0) {
        g_initialize_count++;
        ReleaseSRWLockExclusive(&g_class_lock);
        return S_FALSE;
    }
    g_module = (HINSTANCE)&__ImageBase;
    if (!tinta_shared_graphics_initialize()) {
        g_module = NULL;
        ReleaseSRWLockExclusive(&g_class_lock);
        return E_FAIL;
    }
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_GLOBALCLASS;
    window_class.lpfnWndProc = tinta_control_proc;
    window_class.hInstance = g_module;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.lpszClassName = TINTA_MARKDOWN_VIEW_CLASSW;
    if (!RegisterClassExW(&window_class)) {
        DWORD error = GetLastError();
        if (error == ERROR_CLASS_ALREADY_EXISTS) {
            WNDCLASSEXW existing;
            memset(&existing, 0, sizeof(existing));
            existing.cbSize = sizeof(existing);
            if (GetClassInfoExW(NULL, TINTA_MARKDOWN_VIEW_CLASSW, &existing) &&
                existing.lpfnWndProc == tinta_control_proc)
                result = S_FALSE;
            else
                result = HRESULT_FROM_WIN32(ERROR_CLASS_ALREADY_EXISTS);
        } else {
            result = HRESULT_FROM_WIN32(error);
        }
    }
    if (SUCCEEDED(result)) g_initialize_count = 1;
    else {
        tinta_shared_graphics_uninitialize();
        g_module = NULL;
    }
    ReleaseSRWLockExclusive(&g_class_lock);
    return result;
}

void TintaCoreUninitialize(void) {
    AcquireSRWLockExclusive(&g_class_lock);
    if (g_initialize_count > 0) g_initialize_count--;
    if (!g_initialize_count && !g_live_controls && g_module) {
        UnregisterClassW(TINTA_MARKDOWN_VIEW_CLASSW, g_module);
        g_module = NULL;
        tinta_shared_graphics_uninitialize();
    }
    ReleaseSRWLockExclusive(&g_class_lock);
}
