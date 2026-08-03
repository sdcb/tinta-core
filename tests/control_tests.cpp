#include "tinta_core.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <windows.h>

static int notifications;
static int resource_notifications;
static int link_notifications;
static int document_ready_notifications;
static int stream_update_notifications;
static int autosize_notifications;
static int resource_error_notifications;
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

static bool copy_document_text(HWND view, const wchar_t *markdown,
                               std::wstring *copied) {
    HANDLE data;
    const wchar_t *text;
    if (!view || !markdown || !copied ||
        !SendMessageW(view, WM_SETTEXT, 0,
                      reinterpret_cast<LPARAM>(markdown)))
        return false;
    SendMessageW(view, WM_PAINT, 0, 0);
    SendMessageW(view, TMM_SELECTALL, 0, 0);
    SendMessageW(view, WM_COPY, 0, 0);
    if (!OpenClipboard(view)) return false;
    data = GetClipboardData(CF_UNICODETEXT);
    text = data ? static_cast<const wchar_t *>(GlobalLock(data)) : nullptr;
    if (text) {
        *copied = text;
        GlobalUnlock(data);
    }
    CloseClipboard();
    return text != nullptr;
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
        version.major != 1 ||
        !SendMessageW(view, TMM_GETCAPABILITIES, 0,
                      reinterpret_cast<LPARAM>(&capabilities)) ||
        !(capabilities.flags & TINTA_CAPABILITY_STREAMING)) {
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
    SendMessageW(view, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(L"# Heading\n\nhello world"));
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
    delete[] stream_text;
    if (!stream_text_ok) {
        std::cerr << "stream final text mismatch\n";
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
