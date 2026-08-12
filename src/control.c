#include "internal/control.h"
#include "internal/editor.h"
#include "internal/uia_provider.h"

#include <stdlib.h>
#include <string.h>

extern IMAGE_DOS_HEADER __ImageBase;

static SRWLOCK g_class_lock = SRWLOCK_INIT;
static LONG g_initialize_count;
static LONG g_live_controls;
static HINSTANCE g_module;

static LRESULT CALLBACK tinta_control_proc(HWND hwnd, UINT message,
                                           WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK tinta_control_proc_impl(HWND hwnd, UINT message,
                                                WPARAM wparam, LPARAM lparam);
static void control_destroy_state(TintaControl *control);

static void control_uninitialize_locked(void) {
    if (!g_initialize_count && !g_live_controls && g_module) {
        tinta_editor_unregister_class(g_module);
        UnregisterClassW(TINTA_MARKDOWN_VIEW_CLASSW, g_module);
        g_module = NULL;
        tinta_shared_graphics_uninitialize();
    }
}

void tinta_control_window_created(void) {
    InterlockedIncrement(&g_live_controls);
}

void tinta_control_window_destroyed(void) {
    InterlockedDecrement(&g_live_controls);
    AcquireSRWLockExclusive(&g_class_lock);
    control_uninitialize_locked();
    ReleaseSRWLockExclusive(&g_class_lock);
}

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

static bool system_uses_dark_mode(void) {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &value, &size);
    return status == ERROR_SUCCESS && value == 0;
}

LRESULT tinta_control_notify_parent(TintaControl *control, NMHDR *header) {
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
        case TMM_GETHEADINGCOUNT:
        case TMM_GETHEADING:
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

void tinta_control_notify_code(TintaControl *control, UINT code) {
    NMHDR header;
    memset(&header, 0, sizeof(header));
    header.code = code;
    tinta_control_notify_parent(control, &header);
}

void tinta_control_notify_error(TintaControl *control, HRESULT error,
                                const wchar_t *operation,
                                const wchar_t *message) {
    TintaErrorNotify notice;
    memset(&notice, 0, sizeof(notice));
    notice.hdr.code = TMN_ERROR;
    notice.error = error;
    notice.operation = operation;
    notice.message = message;
    tinta_control_notify_parent(control, &notice.hdr);
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
    tinta_control_notify_parent(control, &notice.hdr);
}

bool tinta_control_apply_theme(TintaControl *control, int index) {
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

bool tinta_control_refresh_system_theme(TintaControl *control) {
    if (!control || !control->use_system_theme) return false;
    return tinta_control_apply_theme(control,
        system_uses_dark_mode() ? TINTA_THEME_MIDNIGHT : TINTA_THEME_PAPER);
}

bool tinta_control_set_custom_theme(TintaControl *control,
                                    const TintaThemeSpec *theme) {
    int old_index;
    const TintaTheme *old_theme;
    if (!control || !theme || theme->cb_size < sizeof(*theme) ||
        !theme->font_family || !theme->code_font_family)
        return false;
    wcsncpy_s(control->custom_font, LF_FACESIZE,
              theme->font_family, _TRUNCATE);
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

bool tinta_control_set_zoom(TintaControl *control, float zoom) {
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
    tinta_control_notify_code(control, TMN_ZOOMCHANGED);
    return true;
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
    control->options.flags = tinta_control_default_option_flags();
    control->view.document_copy_button_enabled =
        (control->options.flags & TINTA_OPTION_DOCUMENT_COPY_BUTTON) != 0;
    control->limits = tinta_control_default_limits();
    control->view.max_document_bytes = control->limits.max_document_bytes;
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
    control->view.resolve_image = tinta_control_resolve_image;
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
    KillTimer(control->view.hwnd, TINTA_TIMER_BLOCK_ANIMATION);
    tinta_stream_async_close(control->stream_async);
    control->stream_async = NULL;
    tinta_str8_destroy(&control->stream_buffer);
    tinta_str16_destroy(&control->base_uri);
    tinta_app_destroy(&control->view);
}

static LRESULT CALLBACK tinta_control_proc_impl(HWND hwnd, UINT message,
                                                WPARAM wparam, LPARAM lparam) {
    TintaControl *control = control_from_window(hwnd);
    LRESULT input_result;
    if (control && control->notification_depth &&
        !control_reentrant_message_allowed(message))
        return FALSE;
    if (message >= TMM_FIRST && message <= TMM_GETPAGEMARGINS && control)
        return tinta_control_dispatch_api(control, message, wparam, lparam);
    if (control && tinta_control_handle_input(
            control, message, wparam, lparam, &input_result))
        return input_result;
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
            tinta_control_window_created();
            return TRUE;
        }
        case WM_CREATE: {
            CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
            if (control && create->lpszName && create->lpszName[0])
                tinta_control_set_text(control, create->lpszName);
            return 0;
        }
        case WM_SETTEXT:
            return control && tinta_control_set_text(
                control, (const wchar_t *)lparam);
        case WM_GETTEXT:
            return control ?
                (LRESULT)min((size_t)wparam - ((size_t)wparam ? 1 : 0),
                    tinta_control_get_text(
                        control, (wchar_t *)lparam, (size_t)wparam)) : 0;
        case WM_GETTEXTLENGTH:
            return control ?
                (LRESULT)tinta_control_get_text(control, NULL, 0) : 0;
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
            if (control) tinta_control_refresh_system_theme(control);
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            if (control && control->redraw_enabled) {
                tinta_render(&control->view);
                tinta_control_layout_completed(control);
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
            return control ?
                tinta_uia_get_object(&control->view, wparam, lparam) : 0;
        case TINTA_WM_LAYOUT_CHUNK:
            if (control) {
                control->view.layout_chunk_posted = false;
                if (tinta_layout_continue(&control->view)) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (!control->view.layout_complete &&
                        !control->view.layout_dirty) {
                        control->view.layout_chunk_posted = PostMessageW(
                            hwnd, TINTA_WM_LAYOUT_CHUNK, 0, 0) != FALSE;
                    } else if (control->view.layout_complete) {
                        tinta_control_layout_completed(control);
                    }
                }
            }
            return 0;
        case TINTA_WM_IMAGE_READY:
            if (control && tinta_remote_image_complete(&control->view))
                control->content_update_pending = true;
            return 0;
        case TINTA_WM_STREAM_PARSED:
            tinta_control_stream_parsed(control);
            return 0;
        case TINTA_WM_UIA_INVOKE:
            if (control && lparam)
                tinta_control_activate_link(control, (const char *)lparam);
            free((void *)lparam);
            return 0;
        case WM_TIMER:
            if (control && wparam == TINTA_TIMER_STREAM) {
                tinta_control_stream_flush(control, false);
                return 0;
            }
            if (control && wparam == TINTA_TIMER_NOTIFICATION) {
                KillTimer(hwnd, TINTA_TIMER_NOTIFICATION);
                control->view.notice_kind = TINTA_NOTICE_NONE;
                control->view.notice_code_block = -1;
                control->view.notice_mermaid_block = -1;
                control->view.notice_svg_block = -1;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (control && wparam == TINTA_TIMER_BLOCK_ANIMATION) {
                if (tinta_block_animation_tick(
                        &control->view, GetTickCount64()))
                    InvalidateRect(hwnd, NULL, FALSE);
                if (!tinta_block_animation_active(&control->view))
                    KillTimer(hwnd, TINTA_TIMER_BLOCK_ANIMATION);
                return 0;
            }
            break;
        case WM_NCDESTROY:
            if (control) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                tinta_uia_disconnect(&control->view);
                tinta_control_window_destroyed();
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
    HRESULT editor_result;
    bool markdown_registered = false;
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
    window_class.style = CS_HREDRAW | CS_VREDRAW |
                         CS_DBLCLKS | CS_GLOBALCLASS;
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
    } else {
        markdown_registered = true;
    }
    editor_result = SUCCEEDED(result) ?
        tinta_editor_register_class(g_module) : result;
    if (FAILED(editor_result)) result = editor_result;
    else if (result == S_OK && editor_result == S_FALSE) result = S_FALSE;
    if (SUCCEEDED(result)) g_initialize_count = 1;
    else {
        if (markdown_registered)
            UnregisterClassW(TINTA_MARKDOWN_VIEW_CLASSW, g_module);
        tinta_shared_graphics_uninitialize();
        g_module = NULL;
    }
    ReleaseSRWLockExclusive(&g_class_lock);
    return result;
}

void TintaCoreUninitialize(void) {
    AcquireSRWLockExclusive(&g_class_lock);
    if (g_initialize_count > 0) g_initialize_count--;
    control_uninitialize_locked();
    ReleaseSRWLockExclusive(&g_class_lock);
}
