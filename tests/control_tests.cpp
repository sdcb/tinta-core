#include "tinta_core.h"

#if TINTA_ENABLE_UIA
#include <UIAutomation.h>
#endif
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
static bool post_quit_on_destroy;
static int destroy_quit_code;

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    (void)hwnd;
    (void)wparam;
    if (message == WM_NOTIFY) {
        NMHDR *header = reinterpret_cast<NMHDR *>(lparam);
        if (header) notifications++;
        if (header && header->code == TMN_DOCUMENTREADY)
            document_ready_notifications++;
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
            return TINTA_RESOURCE_BLOCK;
        }
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
#if TINTA_ENABLE_UIA
    std::wstring accessible_document =
        L"# Accessible heading\n\n[Accessible link](https://example.com)\n\n";
    for (int index = 0; index < 80; index++)
        accessible_document += L"Accessible scrolling content.\n\n";
    SendMessageW(view, WM_SETTEXT, 0,
        reinterpret_cast<LPARAM>(accessible_document.c_str()));
    SendMessageW(view, TMM_GETHEADINGCOUNT, 0, 0);
    SendMessageW(view, TMM_SELECTALL, 0, 0);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IUIAutomation *automation = nullptr;
    IUIAutomationElement *element = nullptr;
    IUIAutomationTextPattern *text_pattern = nullptr;
    IUIAutomationTextRange *range = nullptr;
    IUIAutomationTextRangeArray *selection_ranges = nullptr;
    IUIAutomationScrollPattern *scroll_pattern = nullptr;
    IUIAutomationCondition *link_condition = nullptr;
    IUIAutomationCondition *header_condition = nullptr;
    IUIAutomationElement *link_element = nullptr;
    IUIAutomationElement *header_element = nullptr;
    IUIAutomationInvokePattern *invoke = nullptr;
    BSTR text = nullptr;
    CONTROLTYPEID type = 0;
    HRESULT uia = CoCreateInstance(CLSID_CUIAutomation, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
    if (SUCCEEDED(uia)) uia = automation->ElementFromHandle(view, &element);
    if (SUCCEEDED(uia)) uia = element->get_CurrentControlType(&type);
    if (SUCCEEDED(uia)) uia = element->GetCurrentPatternAs(
        UIA_TextPatternId, IID_PPV_ARGS(&text_pattern));
    if (SUCCEEDED(uia)) uia = text_pattern->get_DocumentRange(&range);
    if (SUCCEEDED(uia)) uia = range->GetText(-1, &text);
    bool uia_ok = SUCCEEDED(uia) && type == UIA_DocumentControlTypeId &&
                  text && std::wcsstr(text, L"Accessible heading");
    int selection_length = 0;
    if (uia_ok) uia = text_pattern->GetSelection(&selection_ranges);
    if (SUCCEEDED(uia) && selection_ranges)
        uia = selection_ranges->get_Length(&selection_length);
    uia_ok = uia_ok && SUCCEEDED(uia) && selection_length == 1;
    BOOL vertically_scrollable = FALSE;
    if (uia_ok) uia = element->GetCurrentPatternAs(
        UIA_ScrollPatternId, IID_PPV_ARGS(&scroll_pattern));
    if (SUCCEEDED(uia) && scroll_pattern)
        uia = scroll_pattern->get_CurrentVerticallyScrollable(
            &vertically_scrollable);
    if (SUCCEEDED(uia) && vertically_scrollable)
        uia = scroll_pattern->Scroll(
            ScrollAmount_NoAmount, ScrollAmount_SmallIncrement);
    TintaScrollPosition uia_scroll{};
    uia_scroll.cb_size = sizeof(uia_scroll);
    SendMessageW(view, TMM_GETSCROLLPOS, 0,
                 reinterpret_cast<LPARAM>(&uia_scroll));
    uia_ok = uia_ok && SUCCEEDED(uia) && vertically_scrollable &&
             uia_scroll.y > 0;
    VARIANT link_type;
    VariantInit(&link_type);
    V_VT(&link_type) = VT_I4;
    V_I4(&link_type) = UIA_HyperlinkControlTypeId;
    if (uia_ok) uia = automation->CreatePropertyCondition(
        UIA_ControlTypePropertyId, link_type, &link_condition);
    if (SUCCEEDED(uia)) uia = element->FindFirst(
        TreeScope_Descendants, link_condition, &link_element);
    VARIANT header_type;
    VariantInit(&header_type);
    V_VT(&header_type) = VT_I4;
    V_I4(&header_type) = UIA_HeaderControlTypeId;
    if (SUCCEEDED(uia)) uia = automation->CreatePropertyCondition(
        UIA_ControlTypePropertyId, header_type, &header_condition);
    if (SUCCEEDED(uia)) uia = element->FindFirst(
        TreeScope_Descendants, header_condition, &header_element);
    if (SUCCEEDED(uia) && link_element) uia = link_element->GetCurrentPatternAs(
        UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
    if (SUCCEEDED(uia) && invoke) uia = invoke->Invoke();
    MSG pending;
    while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pending);
        DispatchMessageW(&pending);
    }
    uia_ok = uia_ok && SUCCEEDED(uia) && link_element && header_element &&
             link_notifications == 1;
    SysFreeString(text);
    if (invoke) invoke->Release();
    if (header_element) header_element->Release();
    if (link_element) link_element->Release();
    if (header_condition) header_condition->Release();
    if (link_condition) link_condition->Release();
    if (scroll_pattern) scroll_pattern->Release();
    if (selection_ranges) selection_ranges->Release();
    if (range) range->Release();
    if (text_pattern) text_pattern->Release();
    if (element) element->Release();
    if (automation) automation->Release();
    if (SUCCEEDED(com)) CoUninitialize();
    if (!uia_ok) {
        std::cerr << "UI Automation text provider failed\n";
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
