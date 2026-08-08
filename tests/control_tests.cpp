#include "tinta_core.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <windows.h>

static int notifications;
static int resource_notifications;
static int link_notifications;
static int document_ready_notifications;
static int stream_update_notifications;
static int autosize_notifications;
static int resource_error_notifications;
static int copy_notifications;
static bool block_resources = true;
static bool post_quit_on_destroy;
static int destroy_quit_code;
static bool destroy_on_ready;
static std::wstring last_resource_uri;

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    (void)hwnd;
    (void)wparam;
    if (message == WM_NOTIFY) {
        NMHDR *header = reinterpret_cast<NMHDR *>(lparam);
        if (header) notifications++;
        if (header && header->code == TMN_DOCUMENTREADY)
            document_ready_notifications++;
        if (header && header->code == TMN_DOCUMENTREADY && destroy_on_ready) {
            destroy_on_ready = false;
            DestroyWindow(header->hwndFrom);
            return 0;
        }
        if (header && header->code == TMN_STREAMUPDATED)
            stream_update_notifications++;
        if (header && header->code == TMN_AUTOSIZED)
            autosize_notifications++;
        if (header && header->code == TMN_COPYCOMPLETED)
            copy_notifications++;
        if (header && header->code == TMN_LINKACTIVATE) {
            link_notifications++;
            return TRUE;
        }
        if (header && header->code == TMN_RESOURCEOPENING) {
            resource_notifications++;
            auto *resource = reinterpret_cast<TintaResourceNotify *>(lparam);
            last_resource_uri = resource->resolved_uri ?
                resource->resolved_uri : L"";
            return block_resources ? TINTA_RESOURCE_BLOCK :
                                     TINTA_RESOURCE_DEFAULT;
        }
        if (header && header->code == TMN_RESOURCEERROR)
            resource_error_notifications++;
    }
    if (message == WM_DESTROY && post_quit_on_destroy) {
        PostQuitMessage(destroy_quit_code);
        post_quit_on_destroy = false;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static bool pump_until(HWND view, int *value, int target, DWORD timeout_ms) {
    ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (*value < target && GetTickCount64() < deadline) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        SendMessageW(view, WM_PAINT, 0, 0);
        Sleep(1);
    }
    return *value >= target;
}

static bool send_control_key(HWND view, WPARAM key) {
    BYTE original[256];
    BYTE keyboard[256];
    if (!GetKeyboardState(original)) return false;
    std::memcpy(keyboard, original, sizeof(keyboard));
    keyboard[VK_CONTROL] |= 0x80;
    if (!SetKeyboardState(keyboard)) return false;
    SendMessageW(view, WM_KEYDOWN, key, 0);
    SetKeyboardState(original);
    return true;
}

static bool read_clipboard_text(HWND owner, std::wstring *copied) {
    if (!copied) return false;
    for (int attempt = 0; attempt < 50; attempt++) {
        HANDLE data;
        const wchar_t *text;
        if (!OpenClipboard(owner)) {
            Sleep(1);
            continue;
        }
        data = GetClipboardData(CF_UNICODETEXT);
        text = data ? static_cast<const wchar_t *>(GlobalLock(data)) : nullptr;
        if (text) {
            *copied = text;
            GlobalUnlock(data);
        }
        CloseClipboard();
        if (text) return true;
        Sleep(1);
    }
    return false;
}

static bool copy_document_text(HWND view, const wchar_t *markdown,
                               std::wstring *copied) {
    if (!view || !markdown || !copied ||
        !SendMessageW(view, WM_SETTEXT, 0,
                      reinterpret_cast<LPARAM>(markdown)))
        return false;
    SendMessageW(view, WM_PAINT, 0, 0);
    SendMessageW(view, TMM_SELECTALL, 0, 0);
    SendMessageW(view, WM_COPY, 0, 0);
    return read_clipboard_text(view, copied);
}

int main() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW parent_class{};
    parent_class.lpfnWndProc = parent_proc;
    parent_class.hInstance = instance;
    parent_class.lpszClassName = L"TintaControlTestParent";
    if (FAILED(TintaCoreInitialize()) || !RegisterClassW(&parent_class)) return 1;
    HWND parent = CreateWindowW(parent_class.lpszClassName, L"test",
        WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, instance, nullptr);
    HWND view = CreateWindowW(TINTA_MARKDOWN_VIEW_CLASSW, L"# Heading\n\nhello world",
        WS_CHILD | WS_VISIBLE, 0, 0, 640, 480, parent,
        reinterpret_cast<HMENU>(100), instance, nullptr);
    if (!parent || !view) {
        std::cerr << "control creation failed\n";
        return 1;
    }
    TintaOptions compiled_options{};
    compiled_options.cb_size = sizeof(compiled_options);
    if (!SendMessageW(view, TMM_GETOPTIONS, 0,
                      reinterpret_cast<LPARAM>(&compiled_options))) {
        std::cerr << "options query failed\n";
        return 1;
    }
    TintaVersionInfo version{};
    version.cb_size = sizeof(version);
    TintaCapabilities capabilities{};
    capabilities.cb_size = sizeof(capabilities);
    if (!SendMessageW(view, TMM_GETVERSION, 0,
                      reinterpret_cast<LPARAM>(&version)) ||
        version.major != 1 || version.minor != 1 ||
        !SendMessageW(view, TMM_GETCAPABILITIES, 0,
                      reinterpret_cast<LPARAM>(&capabilities)) ||
        !(capabilities.flags & TINTA_CAPABILITY_STREAMING) ||
        !(capabilities.option_flags & TINTA_OPTION_DOCUMENT_COPY_BUTTON) ||
        (compiled_options.flags & TINTA_OPTION_DOCUMENT_COPY_BUTTON)) {
        std::cerr << "version/capability query failed\n";
        return 1;
    }
    float zoom = 0.0f;
    float requested_zoom = 1.4f;
    if (!SendMessageW(view, TMM_GETZOOM, 0,
                      reinterpret_cast<LPARAM>(&zoom)) ||
        std::fabs(zoom - 1.0f) > 0.001f ||
        !SendMessageW(view, TMM_SETZOOM, 0,
                      reinterpret_cast<LPARAM>(&requested_zoom)) ||
        !SendMessageW(view, TMM_GETZOOM, 0,
                      reinterpret_cast<LPARAM>(&zoom)) ||
        std::fabs(zoom - 1.4f) > 0.001f ||
        !send_control_key(view, '0') ||
        !SendMessageW(view, TMM_GETZOOM, 0,
                      reinterpret_cast<LPARAM>(&zoom)) ||
        std::fabs(zoom - 1.0f) > 0.001f ||
        !send_control_key(view, VK_OEM_PLUS) ||
        !SendMessageW(view, TMM_GETZOOM, 0,
                      reinterpret_cast<LPARAM>(&zoom)) ||
        std::fabs(zoom - 1.1f) > 0.001f ||
        !send_control_key(view, VK_OEM_MINUS) ||
        !SendMessageW(view, TMM_GETZOOM, 0,
                      reinterpret_cast<LPARAM>(&zoom)) ||
        std::fabs(zoom - 1.0f) > 0.001f) {
        std::cerr << "zoom API or keyboard shortcuts failed\n";
        return 1;
    }
    std::wstring copied_text;
    if (!copy_document_text(view, L"- first\n- second",
                            &copied_text) ||
        copied_text != L"\x2022 first\n\x2022 second\n" ||
        !copy_document_text(view,
                            L"- first paragraph\n\n- second paragraph",
                            &copied_text) ||
        copied_text != L"\x2022 first paragraph\n\n\x2022 second paragraph\n\n") {
        std::cerr << "list clipboard spacing failed\n";
        return 1;
    }
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    UpdateWindow(parent);
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(L"```text\ncopy me\n```"));
    SendMessageW(view, WM_PAINT, 0, 0);
    SendMessageW(view, TMM_SELECTALL, 0, 0);
    SendMessageW(view, WM_COPY, 0, 0);
    if (!read_clipboard_text(view, &copied_text) ||
        copied_text != L"copy me\n\n") {
        std::cerr << "code language header leaked into document text\n";
        return 1;
    }
    SendMessageW(view, TMM_CLEARSELECTION, 0, 0);
    ValidateRect(view, nullptr);
    float dpi_scale = GetDpiForWindow(view) / 96.0f;
    RECT code_client{};
    GetClientRect(view, &code_client);
    int copy_x = code_client.right - static_cast<int>(74.0f * dpi_scale);
    int copy_y = static_cast<int>(41.0f * dpi_scale);
    int code_hover_x = static_cast<int>(60.0f * dpi_scale);
    SendMessageW(view, WM_MOUSEMOVE, 0,
                 MAKELPARAM(code_hover_x, copy_y));
    if (!GetUpdateRect(view, nullptr, FALSE)) {
        std::cerr << "code block hover did not invalidate\n";
        return 1;
    }
    SendMessageW(view, WM_PAINT, 0, 0);
    ValidateRect(view, nullptr);
    SendMessageW(view, WM_MOUSEMOVE, 0, MAKELPARAM(copy_x, copy_y));
    if (!GetUpdateRect(view, nullptr, FALSE)) {
        std::cerr << "code copy button hover transition did not invalidate\n";
        return 1;
    }
    SendMessageW(view, WM_PAINT, 0, 0);
    int copy_target = copy_notifications + 1;
    SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(copy_x, copy_y));
    if (!read_clipboard_text(view, &copied_text) ||
        copied_text != L"copy me\n" ||
        copy_notifications != copy_target) {
        std::cerr << "code copy button did not copy on first hover\n";
        return 1;
    }
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(L"# Heading\n\nhello world"));
    SendMessageW(view, WM_PAINT, 0, 0);
    int document_copy_x = code_client.right -
        static_cast<int>(51.0f * dpi_scale);
    int document_copy_y = static_cast<int>(26.0f * dpi_scale);
    int disabled_copy_target = copy_notifications;
    SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(document_copy_x, document_copy_y));
    if (!read_clipboard_text(view, &copied_text) ||
        copied_text != L"copy me\n" ||
        copy_notifications != disabled_copy_target) {
        std::cerr << "disabled document copy button was active\n";
        return 1;
    }
    const wchar_t *document_markdown =
        L"# Heading\r\n\r\n**bold** caf\u00e9 and [link](https://example.test)\r\n";
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(document_markdown));
    TintaOptions document_options = compiled_options;
    document_options.flags |= TINTA_OPTION_DOCUMENT_COPY_BUTTON;
    if (!SendMessageW(view, TMM_SETOPTIONS, 0,
                      reinterpret_cast<LPARAM>(&document_options)) ||
        !SendMessageW(view, TMM_GETOPTIONS, 0,
                      reinterpret_cast<LPARAM>(&document_options)) ||
        !(document_options.flags & TINTA_OPTION_DOCUMENT_COPY_BUTTON)) {
        std::cerr << "document copy option failed\n";
        return 1;
    }
    SendMessageW(view, WM_MOUSELEAVE, 0, 0);
    SendMessageW(view, WM_PAINT, 0, 0);
    ValidateRect(view, nullptr);
    SendMessageW(view, WM_MOUSEMOVE, 0,
                 MAKELPARAM(document_copy_x, document_copy_y));
    if (!GetUpdateRect(view, nullptr, FALSE)) {
        std::cerr << "document copy hover did not invalidate\n";
        return 1;
    }
    SendMessageW(view, WM_PAINT, 0, 0);
    int document_copy_target = copy_notifications + 1;
    SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(document_copy_x, document_copy_y));
    if (!read_clipboard_text(view, &copied_text) ||
        copied_text != document_markdown ||
        copy_notifications != document_copy_target) {
        std::cerr << "document copy button did not preserve Markdown source\n";
        return 1;
    }
    const wchar_t *overlap_markdown = L"```text\ncode only\n```";
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(overlap_markdown));
    SendMessageW(view, WM_PAINT, 0, 0);
    SendMessageW(view, WM_MOUSEMOVE, 0,
                 MAKELPARAM(document_copy_x, document_copy_y));
    document_copy_target = copy_notifications + 1;
    SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(document_copy_x, document_copy_y));
    if (!read_clipboard_text(view, &copied_text) ||
        copied_text != overlap_markdown ||
        copy_notifications != document_copy_target) {
        std::cerr << "document copy button did not take click priority\n";
        return 1;
    }
    std::wstring scrolling_markdown = L"# Fixed copy button\n\n";
    for (int line = 0; line < 80; line++)
        scrolling_markdown += L"- scrolling line\n";
    int scroll_ready_target = document_ready_notifications + 1;
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(scrolling_markdown.c_str()));
    if (!pump_until(view, &document_ready_notifications,
                    scroll_ready_target, 2000)) {
        std::cerr << "scrolling document did not finish layout\n";
        return 1;
    }
    TintaScrollPosition scroll_position{};
    scroll_position.cb_size = sizeof(scroll_position);
    scroll_position.y = 200.0f;
    if (!SendMessageW(view, TMM_SETSCROLLPOS, 0,
                      reinterpret_cast<LPARAM>(&scroll_position))) {
        std::cerr << "document copy scroll setup failed\n";
        return 1;
    }
    SendMessageW(view, WM_PAINT, 0, 0);
    scroll_position.y = 0;
    if (!SendMessageW(view, TMM_GETSCROLLPOS, 0,
                      reinterpret_cast<LPARAM>(&scroll_position)) ||
        scroll_position.y <= 0) {
        std::cerr << "document did not scroll\n";
        return 1;
    }
    document_copy_x = code_client.right -
        static_cast<int>(59.0f * dpi_scale);
    int scrolled_copy_target = copy_notifications;
    SendMessageW(view, WM_MOUSEMOVE, 0,
                 MAKELPARAM(document_copy_x, document_copy_y));
    SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(document_copy_x, document_copy_y));
    if (!read_clipboard_text(view, &copied_text) ||
        copied_text != overlap_markdown ||
        copy_notifications != scrolled_copy_target) {
        std::cerr << "document copy button followed the vertical scroll\n";
        return 1;
    }
    scroll_position.y = 0;
    if (!SendMessageW(view, TMM_SETSCROLLPOS, 0,
                      reinterpret_cast<LPARAM>(&scroll_position))) {
        std::cerr << "document copy scroll reset failed\n";
        return 1;
    }
    SendMessageW(view, WM_PAINT, 0, 0);
    SendMessageW(view, WM_MOUSEMOVE, 0,
                 MAKELPARAM(document_copy_x, document_copy_y));
    document_copy_target = copy_notifications + 1;
    SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(document_copy_x, document_copy_y));
    if (!read_clipboard_text(view, &copied_text) ||
        copied_text != scrolling_markdown ||
        copy_notifications != document_copy_target) {
        std::cerr << "document copy button was not available at the top\n";
        return 1;
    }
    document_options.flags &= ~TINTA_OPTION_DOCUMENT_COPY_BUTTON;
    if (!SendMessageW(view, TMM_SETOPTIONS, 0,
                      reinterpret_cast<LPARAM>(&document_options))) {
        std::cerr << "document copy option disable failed\n";
        return 1;
    }
    int disabled_after_copy_target = copy_notifications;
    SendMessageW(view, WM_MOUSEMOVE, 0,
                 MAKELPARAM(document_copy_x, document_copy_y));
    SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(document_copy_x, document_copy_y));
    if (!read_clipboard_text(view, &copied_text)) {
        std::cerr << "clipboard read failed after document copy disable\n";
        return 1;
    }
    if (copied_text != scrolling_markdown) {
        std::cerr << "clipboard changed after document copy disable\n";
        return 1;
    }
    if (copy_notifications != disabled_after_copy_target) {
        std::cerr << "copy notification fired after document copy disable\n";
        return 1;
    }
    ShowWindow(parent, SW_HIDE);
#if !TINTA_ENABLE_LOCAL_IMAGES
    if (compiled_options.flags & TINTA_OPTION_LOCAL_IMAGES) {
        std::cerr << "disabled local images were reported as supported\n";
        return 1;
    }
#endif
#if !TINTA_ENABLE_REMOTE_IMAGES
    if (compiled_options.flags & TINTA_OPTION_REMOTE_IMAGES) {
        std::cerr << "disabled remote images were reported as supported\n";
        return 1;
    }
#endif
    TintaOptions requested_options = compiled_options;
    requested_options.flags |= TINTA_OPTION_LOCAL_IMAGES |
                               TINTA_OPTION_REMOTE_IMAGES;
    if (!SendMessageW(view, TMM_SETOPTIONS, 0,
                      reinterpret_cast<LPARAM>(&requested_options)) ||
        !SendMessageW(view, TMM_GETOPTIONS, 0,
                      reinterpret_cast<LPARAM>(&compiled_options))) {
        std::cerr << "options update failed\n";
        return 1;
    }
#if !TINTA_ENABLE_LOCAL_IMAGES
    if (compiled_options.flags & TINTA_OPTION_LOCAL_IMAGES) {
        std::cerr << "disabled local images could be enabled at runtime\n";
        return 1;
    }
#endif
#if !TINTA_ENABLE_REMOTE_IMAGES
    if (compiled_options.flags & TINTA_OPTION_REMOTE_IMAGES) {
        std::cerr << "disabled remote images could be enabled at runtime\n";
        return 1;
    }
#endif
    TintaLimits limits{};
    limits.cb_size = sizeof(limits);
    if (!SendMessageW(view, TMM_GETLIMITS, 0,
                      reinterpret_cast<LPARAM>(&limits))) {
        std::cerr << "limits query failed\n";
        return 1;
    }
    TintaLimits restricted = limits;
    restricted.max_document_bytes = 4;
    if (!SendMessageW(view, TMM_SETLIMITS, 0,
                      reinterpret_cast<LPARAM>(&restricted)) ||
        SendMessageW(view, WM_SETTEXT, 0,
                     reinterpret_cast<LPARAM>(L"too large"))) {
        std::cerr << "document size limit failed\n";
        return 1;
    }
    restricted = limits;
    restricted.max_ast_nodes = 1;
    if (!SendMessageW(view, TMM_SETLIMITS, 0,
                      reinterpret_cast<LPARAM>(&restricted)) ||
        SendMessageW(view, WM_SETTEXT, 0,
                     reinterpret_cast<LPARAM>(L"# node"))) {
        std::cerr << "AST node limit failed\n";
        return 1;
    }
    SendMessageW(view, TMM_SETLIMITS, 0, reinterpret_cast<LPARAM>(&limits));
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(L"# Heading\n\nhello world"));
    SendMessageW(view, WM_PAINT, 0, 0);
    if (SendMessageW(view, WM_GETTEXTLENGTH, 0, 0) == 0 ||
        SendMessageW(view, TMM_GETHEADINGCOUNT, 0, 0) != 1) {
        std::cerr << "document or heading API failed\n";
        return 1;
    }
    TintaStats stats{};
    stats.cb_size = sizeof(stats);
    if (!SendMessageW(view, TMM_GETSTATS, 0,
                      reinterpret_cast<LPARAM>(&stats)) ||
        !stats.document_revision || !stats.source_bytes || !stats.ast_nodes) {
        std::cerr << "statistics query failed\n";
        return 1;
    }
    TintaPageMargins default_margins{};
    default_margins.cb_size = sizeof(default_margins);
    if (!SendMessageW(view, TMM_GETPAGEMARGINS, 0,
                      reinterpret_cast<LPARAM>(&default_margins)) ||
        default_margins.left != 40.0f || default_margins.top != 20.0f ||
        default_margins.right != 40.0f || default_margins.bottom != 40.0f) {
        std::cerr << "default page margins failed\n";
        return 1;
    }
    TintaPageMargins invalid_margins = default_margins;
    invalid_margins.left = -1.0f;
    if (SendMessageW(view, TMM_SETPAGEMARGINS, 0,
                     reinterpret_cast<LPARAM>(&invalid_margins))) {
        std::cerr << "negative page margin was accepted\n";
        return 1;
    }
    invalid_margins = default_margins;
    invalid_margins.top = std::numeric_limits<float>::quiet_NaN();
    if (SendMessageW(view, TMM_SETPAGEMARGINS, 0,
                     reinterpret_cast<LPARAM>(&invalid_margins))) {
        std::cerr << "non-finite page margin was accepted\n";
        return 1;
    }
    std::wstring margin_document;
    for (int word = 0; word < 240; word++) margin_document += L"margin word ";
    int margin_ready_target = document_ready_notifications + 1;
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(margin_document.c_str()));
    if (!pump_until(view, &document_ready_notifications,
                    margin_ready_target, 2000)) {
        std::cerr << "page margin test document did not finish layout\n";
        return 1;
    }
    TintaContentSize default_margin_size{};
    default_margin_size.cb_size = sizeof(default_margin_size);
    if (!SendMessageW(view, TMM_GETCONTENTSIZE, 0,
                      reinterpret_cast<LPARAM>(&default_margin_size))) {
        std::cerr << "default margin content size failed\n";
        return 1;
    }
    TintaPageMargins compact_margins{};
    compact_margins.cb_size = sizeof(compact_margins);
    compact_margins.left = 8.0f;
    compact_margins.top = 6.0f;
    compact_margins.right = 10.0f;
    compact_margins.bottom = 12.0f;
    if (!SendMessageW(view, TMM_SETPAGEMARGINS, 0,
                      reinterpret_cast<LPARAM>(&compact_margins))) {
        std::cerr << "compact page margins failed\n";
        return 1;
    }
    SendMessageW(view, WM_PAINT, 0, 0);
    TintaContentSize compact_margin_size{};
    compact_margin_size.cb_size = sizeof(compact_margin_size);
    TintaPageMargins queried_margins{};
    queried_margins.cb_size = sizeof(queried_margins);
    if (!SendMessageW(view, TMM_GETCONTENTSIZE, 0,
                      reinterpret_cast<LPARAM>(&compact_margin_size)) ||
        !SendMessageW(view, TMM_GETPAGEMARGINS, 0,
                      reinterpret_cast<LPARAM>(&queried_margins)) ||
        queried_margins.left != compact_margins.left ||
        queried_margins.top != compact_margins.top ||
        queried_margins.right != compact_margins.right ||
        queried_margins.bottom != compact_margins.bottom ||
        compact_margin_size.height >= default_margin_size.height) {
        std::cerr << "page margins did not affect layout\n";
        return 1;
    }
    if (!SendMessageW(view, TMM_SETPAGEMARGINS, 0,
                      reinterpret_cast<LPARAM>(&default_margins))) {
        std::cerr << "page margin restore failed\n";
        return 1;
    }
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(L"# Heading\n\nhello world"));
    SendMessageW(view, WM_PAINT, 0, 0);
    TintaFindRequest find{};
    find.cb_size = sizeof(find);
    find.text = L"hello";
    find.text_length = 5;
    find.flags = TINTA_FIND_WRAP;
    TintaFindState state{};
    state.cb_size = sizeof(state);
    if (!SendMessageW(view, TMM_FIND, 0, reinterpret_cast<LPARAM>(&find)) ||
        !SendMessageW(view, TMM_GETFINDSTATE, 0, reinterpret_cast<LPARAM>(&state)) ||
        state.match_count != 1) {
        std::cerr << "find API failed\n";
        return 1;
    }
    SendMessageW(view, TMM_SELECTALL, 0, 0);
    TintaSelection selection{};
    selection.cb_size = sizeof(selection);
    if (!SendMessageW(view, TMM_GETSELECTION, 0,
                      reinterpret_cast<LPARAM>(&selection)) ||
        selection.end <= selection.start) {
        std::cerr << "selection API failed\n";
        return 1;
    }
    TintaAutoSize auto_size{};
    auto_size.cb_size = sizeof(auto_size);
    auto_size.flags = TINTA_AUTOSIZE_HEIGHT |
                      TINTA_AUTOSIZE_MAX_HEIGHT;
    auto_size.min_height = 80.0f;
    auto_size.max_height = 120.0f;
    int autosize_target = autosize_notifications + 1;
    if (!SendMessageW(view, TMM_SETAUTOSIZE, 0,
                      reinterpret_cast<LPARAM>(&auto_size)) ||
        autosize_notifications < autosize_target) {
        std::cerr << "autosize configuration failed\n";
        return 1;
    }
    std::wstring tall_document = L"# Auto size\n\n";
    for (int index = 0; index < 100; index++)
        tall_document += L"A line that makes the document taller.\n\n";
    int autosize_ready_target = document_ready_notifications + 1;
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(tall_document.c_str()));
    if (!pump_until(view, &document_ready_notifications,
                    autosize_ready_target, 3000)) {
        std::cerr << "autosize document layout failed\n";
        return 1;
    }
    RECT autosized_client{};
    GetClientRect(view, &autosized_client);
    int expected_maximum = MulDiv(120, static_cast<int>(GetDpiForWindow(view)), 96);
    if (autosized_client.bottom - autosized_client.top > expected_maximum + 1) {
        std::cerr << "autosize maximum height failed\n";
        return 1;
    }
    TintaAutoSize queried_auto_size{};
    queried_auto_size.cb_size = sizeof(queried_auto_size);
    if (!SendMessageW(view, TMM_GETAUTOSIZE, 0,
                      reinterpret_cast<LPARAM>(&queried_auto_size)) ||
        queried_auto_size.flags != auto_size.flags ||
        queried_auto_size.max_height != auto_size.max_height) {
        std::cerr << "autosize query failed\n";
        return 1;
    }
    auto_size.flags = 0;
    if (!SendMessageW(view, TMM_SETAUTOSIZE, 0,
                      reinterpret_cast<LPARAM>(&auto_size))) {
        std::cerr << "autosize disable failed\n";
        return 1;
    }
    SetWindowPos(view, nullptr, 0, 0, 640, 480,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    TintaOptions stream_options{};
    stream_options.cb_size = sizeof(stream_options);
    if (!SendMessageW(view, TMM_GETOPTIONS, 0,
                      reinterpret_cast<LPARAM>(&stream_options))) {
        std::cerr << "stream options query failed\n";
        return 1;
    }
    stream_options.flags |= TINTA_OPTION_DOCUMENT_COPY_BUTTON;
    if (!SendMessageW(view, TMM_SETOPTIONS, 0,
                      reinterpret_cast<LPARAM>(&stream_options))) {
        std::cerr << "stream document copy option failed\n";
        return 1;
    }
    TintaStreamBegin stream_begin{};
    stream_begin.cb_size = sizeof(stream_begin);
    stream_begin.base_uri = L"C:\\temp\\stream.md";
    stream_begin.format = TINTA_FORMAT_MARKDOWN;
    stream_begin.refresh_interval_ms = 20;
    if (!SendMessageW(view, TMM_STREAM_BEGIN, 0,
                      reinterpret_cast<LPARAM>(&stream_begin))) {
        std::cerr << "stream begin failed\n";
        return 1;
    }
    const char stream_prefix[] = "# Stream\n\n![cached](missing.png)\n\ncaf";
    TintaStreamChunk stream_chunk{};
    stream_chunk.cb_size = sizeof(stream_chunk);
    stream_chunk.utf8 = stream_prefix;
    stream_chunk.utf8_length = std::strlen(stream_prefix);
    if (!SendMessageW(view, TMM_STREAM_APPEND, 0,
                      reinterpret_cast<LPARAM>(&stream_chunk))) {
        std::cerr << "stream prefix failed\n";
        return 1;
    }
    const char utf8_lead[] = {static_cast<char>(0xc3)};
    stream_chunk.utf8 = utf8_lead;
    stream_chunk.utf8_length = sizeof(utf8_lead);
    if (!SendMessageW(view, TMM_STREAM_APPEND, 0,
                      reinterpret_cast<LPARAM>(&stream_chunk))) {
        std::cerr << "split UTF-8 lead failed\n";
        return 1;
    }
    const char invalid[] = {static_cast<char>(0xff)};
    stream_chunk.utf8 = invalid;
    if (SendMessageW(view, TMM_STREAM_APPEND, 0,
                     reinterpret_cast<LPARAM>(&stream_chunk))) {
        std::cerr << "invalid UTF-8 was accepted\n";
        return 1;
    }
    const char utf8_tail[] = {static_cast<char>(0xa9)};
    stream_chunk.utf8 = utf8_tail;
    if (!SendMessageW(view, TMM_STREAM_APPEND, 0,
                      reinterpret_cast<LPARAM>(&stream_chunk))) {
        std::cerr << "split UTF-8 tail failed\n";
        return 1;
    }
    const char delta[] = " more";
    stream_chunk.utf8 = delta;
    stream_chunk.utf8_length = std::strlen(delta);
    for (int index = 0; index < 1000; index++) {
        if (!SendMessageW(view, TMM_STREAM_APPEND, 0,
                          reinterpret_cast<LPARAM>(&stream_chunk))) {
            std::cerr << "high frequency stream append failed\n";
            return 1;
        }
    }
    int ready_target = document_ready_notifications + 1;
    if (!SendMessageW(view, TMM_STREAM_END, 0, 0) ||
        !pump_until(view, &document_ready_notifications, ready_target, 3000)) {
        std::cerr << "stream end failed\n";
        return 1;
    }
    if (stream_update_notifications >= 1000 ||
        resource_notifications != (TINTA_ENABLE_LOCAL_IMAGES ? 1 : 0)) {
        std::cerr << "stream coalescing or image cache failed\n";
        return 1;
    }
    int stream_text_length = GetWindowTextLengthW(view);
    wchar_t *stream_text = new wchar_t[static_cast<size_t>(stream_text_length) + 1];
    GetWindowTextW(view, stream_text, stream_text_length + 1);
    bool stream_text_ok = std::wcsstr(stream_text, L"caf\u00e9 more") != nullptr;
    std::wstring displayed_stream_markdown(stream_text);
    delete[] stream_text;
    if (!stream_text_ok) {
        std::cerr << "stream final text mismatch\n";
        return 1;
    }
    TintaContentSize stream_content{};
    stream_content.cb_size = sizeof(stream_content);
    if (!SendMessageW(view, TMM_GETCONTENTSIZE, 0,
                      reinterpret_cast<LPARAM>(&stream_content))) {
        std::cerr << "stream content size query failed\n";
        return 1;
    }
    RECT stream_client{};
    GetClientRect(view, &stream_client);
    float stream_right_padding =
        stream_content.height > stream_client.bottom ? 20.0f : 12.0f;
    int stream_copy_x = stream_client.right - static_cast<int>(
        (stream_right_padding + 39.0f) * dpi_scale);
    SendMessageW(view, WM_MOUSEMOVE, 0,
                 MAKELPARAM(stream_copy_x, document_copy_y));
    int stream_copy_target = copy_notifications + 1;
    SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(stream_copy_x, document_copy_y));
    if (!read_clipboard_text(view, &copied_text) ||
        copied_text != displayed_stream_markdown ||
        copy_notifications != stream_copy_target) {
        std::cerr << "stream document copy did not use displayed revision\n";
        return 1;
    }
    RECT fixed_client{};
    GetClientRect(view, &fixed_client);
    if (fixed_client.bottom - fixed_client.top != 480) {
        std::cerr << "disabled autosize changed the fixed height\n";
        return 1;
    }
    TintaDocument image_document{};
    const char image_markdown[] = "# Image\n\n![](missing.png)\n";
    image_document.cb_size = sizeof(image_document);
    image_document.utf8 = image_markdown;
    image_document.utf8_length = std::strlen(image_markdown);
    image_document.base_uri = L"C:\\temp\\document.md";
    image_document.format = TINTA_FORMAT_MARKDOWN;
    if (!SendMessageW(view, TMM_SETDOCUMENT, 0,
                      reinterpret_cast<LPARAM>(&image_document))) {
        std::cerr << "image document failed\n";
        return 1;
    }
    SendMessageW(view, TMM_GETHEADINGCOUNT, 0, 0);
    if (resource_notifications != (TINTA_ENABLE_LOCAL_IMAGES ? 2 : 0)) {
        std::cerr << "resource notification failed\n";
        return 1;
    }
    TintaFindRequest image_find{};
    image_find.cb_size = sizeof(image_find);
    image_find.text = L"Image: missing.png";
    image_find.text_length = std::wcslen(image_find.text);
    TintaFindState image_find_state{};
    image_find_state.cb_size = sizeof(image_find_state);
    if (!SendMessageW(view, TMM_FIND, 0,
                      reinterpret_cast<LPARAM>(&image_find)) ||
        !SendMessageW(view, TMM_GETFINDSTATE, 0,
                      reinterpret_cast<LPARAM>(&image_find_state)) ||
        image_find_state.match_count != 1) {
        std::cerr << "image link fallback text failed\n";
        return 1;
    }
#if TINTA_ENABLE_LOCAL_IMAGES
    if (!SendMessageW(view, TMM_SETBASEURI, 0,
                      reinterpret_cast<LPARAM>(L"C:\\stable\\document.md"))) {
        std::cerr << "base URI setup failed\n";
        return 1;
    }
    restricted = limits;
    restricted.max_ast_nodes = 1;
    SendMessageW(view, TMM_SETLIMITS, 0,
                 reinterpret_cast<LPARAM>(&restricted));
    TintaDocument rejected_document{};
    rejected_document.cb_size = sizeof(rejected_document);
    rejected_document.utf8 = "# rejected";
    rejected_document.utf8_length = std::strlen(rejected_document.utf8);
    rejected_document.base_uri = L"C:\\replacement\\document.md";
    rejected_document.format = TINTA_FORMAT_MARKDOWN;
    if (SendMessageW(view, TMM_SETDOCUMENT, 0,
                     reinterpret_cast<LPARAM>(&rejected_document))) {
        std::cerr << "transactional document rejection failed\n";
        return 1;
    }
    SendMessageW(view, TMM_SETLIMITS, 0, reinterpret_cast<LPARAM>(&limits));
    last_resource_uri.clear();
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(L"![](probe.png)"));
    SendMessageW(view, WM_PAINT, 0, 0);
    if (last_resource_uri.find(L"C:\\stable\\probe.png") == std::wstring::npos) {
        std::cerr << "failed document changed the committed base URI\n";
        return 1;
    }
#endif
    constexpr UINT image_ready_message = WM_APP + 2;
    HWND first_probe = CreateWindowW(TINTA_MARKDOWN_VIEW_CLASSW, L"first",
        WS_CHILD, 0, 0, 1, 1, parent, reinterpret_cast<HMENU>(101),
        instance, nullptr);
    HWND second_probe = CreateWindowW(TINTA_MARKDOWN_VIEW_CLASSW, L"second",
        WS_CHILD, 0, 0, 1, 1, parent, reinterpret_cast<HMENU>(102),
        instance, nullptr);
    if (!first_probe || !second_probe ||
        !PostMessageW(second_probe, image_ready_message, 0, 0)) {
        std::cerr << "multi-control setup failed\n";
        return 1;
    }
    DestroyWindow(first_probe);
    MSG image_message{};
    if (!PeekMessageW(&image_message, second_probe, image_ready_message,
                      image_ready_message, PM_REMOVE)) {
        std::cerr << "destroying one control consumed another control's image message\n";
        return 1;
    }
    DestroyWindow(second_probe);
#if TINTA_ENABLE_LOCAL_IMAGES
    block_resources = false;
    HWND error_probe = CreateWindowW(TINTA_MARKDOWN_VIEW_CLASSW,
        L"![](C:\\path-that-does-not-exist\\missing.png)", WS_CHILD,
        0, 0, 320, 100, parent, reinterpret_cast<HMENU>(104),
        instance, nullptr);
    if (!error_probe) {
        std::cerr << "resource error setup failed\n";
        return 1;
    }
    SendMessageW(error_probe, WM_PAINT, 0, 0);
    if (!resource_error_notifications) {
        std::cerr << "resource error notification failed\n";
        return 1;
    }
    DestroyWindow(error_probe);
    block_resources = true;
#endif
    HWND reentrant_probe = CreateWindowW(TINTA_MARKDOWN_VIEW_CLASSW, L"first",
        WS_CHILD, 0, 0, 320, 100, parent, reinterpret_cast<HMENU>(103),
        instance, nullptr);
    if (!reentrant_probe) {
        std::cerr << "reentrant notification setup failed\n";
        return 1;
    }
    destroy_on_ready = true;
    SendMessageW(reentrant_probe, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(L"# destroy during notification"));
    SendMessageW(reentrant_probe, WM_PAINT, 0, 0);
    if (IsWindow(reentrant_probe)) {
        std::cerr << "notification-time destruction was not handled\n";
        return 1;
    }
    post_quit_on_destroy = true;
    destroy_quit_code = 37;
    UINT_PTR shutdown_timer = SetTimer(nullptr, 0, 2000, nullptr);
    bool received_quit = false;
    if (!shutdown_timer || !PostMessageW(parent, WM_CLOSE, 0, 0)) {
        std::cerr << "failed to start control shutdown test\n";
        return 1;
    }
    for (;;) {
        MSG shutdown_message{};
        BOOL result = GetMessageW(&shutdown_message, nullptr, 0, 0);
        if (result == 0) {
            received_quit = shutdown_message.wParam == 37;
            break;
        }
        if (result < 0 ||
            (shutdown_message.message == WM_TIMER &&
             shutdown_message.hwnd == nullptr &&
             shutdown_message.wParam == shutdown_timer))
            break;
        TranslateMessage(&shutdown_message);
        DispatchMessageW(&shutdown_message);
    }
    KillTimer(nullptr, shutdown_timer);
    if (!received_quit) {
        std::cerr << "destroying a control consumed the host's quit message\n";
        return 1;
    }
    TintaCoreUninitialize();
    std::cout << "Control tests passed\n";
    return 0;
}
