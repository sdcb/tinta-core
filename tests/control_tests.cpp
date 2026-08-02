#include "tinta_core.h"

#include <UIAutomation.h>
#include <cstring>
#include <iostream>
#include <windows.h>

static int notifications;
static int resource_notifications;
static int link_notifications;

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    (void)hwnd;
    (void)wparam;
    if (message == WM_NOTIFY) {
        NMHDR *header = reinterpret_cast<NMHDR *>(lparam);
        if (header) notifications++;
        if (header && header->code == TMN_LINKACTIVATE) {
            link_notifications++;
            return TRUE;
        }
        if (header && header->code == TMN_RESOURCEOPENING) {
            resource_notifications++;
            return TINTA_RESOURCE_BLOCK;
        }
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
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
    TintaDocument image_document{};
    const char image_markdown[] = "# Image\n\n![alt](missing.png)\n";
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
    if (resource_notifications != 1) {
        std::cerr << "resource notification failed\n";
        return 1;
    }
    SendMessageW(view, WM_SETTEXT, 0,
        reinterpret_cast<LPARAM>(L"# Accessible heading\n\n[Accessible link](https://example.com)"));
    SendMessageW(view, TMM_GETHEADINGCOUNT, 0, 0);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IUIAutomation *automation = nullptr;
    IUIAutomationElement *element = nullptr;
    IUIAutomationTextPattern *text_pattern = nullptr;
    IUIAutomationTextRange *range = nullptr;
    IUIAutomationCondition *link_condition = nullptr;
    IUIAutomationElement *link_element = nullptr;
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
    VARIANT link_type;
    VariantInit(&link_type);
    V_VT(&link_type) = VT_I4;
    V_I4(&link_type) = UIA_HyperlinkControlTypeId;
    if (uia_ok) uia = automation->CreatePropertyCondition(
        UIA_ControlTypePropertyId, link_type, &link_condition);
    if (SUCCEEDED(uia)) uia = element->FindFirst(
        TreeScope_Descendants, link_condition, &link_element);
    if (SUCCEEDED(uia) && link_element) uia = link_element->GetCurrentPatternAs(
        UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
    if (SUCCEEDED(uia) && invoke) uia = invoke->Invoke();
    MSG pending;
    while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pending);
        DispatchMessageW(&pending);
    }
    uia_ok = uia_ok && SUCCEEDED(uia) && link_element && link_notifications == 1;
    SysFreeString(text);
    if (invoke) invoke->Release();
    if (link_element) link_element->Release();
    if (link_condition) link_condition->Release();
    if (range) range->Release();
    if (text_pattern) text_pattern->Release();
    if (element) element->Release();
    if (automation) automation->Release();
    if (SUCCEEDED(com)) CoUninitialize();
    if (!uia_ok) {
        std::cerr << "UI Automation text provider failed\n";
        return 1;
    }
    DestroyWindow(parent);
    TintaCoreUninitialize();
    std::cout << "Control tests passed\n";
    return 0;
}
