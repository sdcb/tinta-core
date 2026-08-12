#include "tinta_core.h"

#include <UIAutomation.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <windows.h>

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void pump_messages() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

int main() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW parent_class{};
    parent_class.lpfnWndProc = parent_proc;
    parent_class.hInstance = instance;
    parent_class.lpszClassName = L"TintaEditorUiaTestParent";
    if (FAILED(TintaCoreInitialize()) || !RegisterClassW(&parent_class))
        return 1;
    HWND parent = CreateWindowW(parent_class.lpszClassName, L"uia",
        WS_OVERLAPPEDWINDOW, 0, 0, 640, 320, nullptr, nullptr, instance,
        nullptr);
    std::wstring input = L"first\nsecond 😀\n";
    for (int index = 0; index < 100; index++) input += L"scroll line\n";
    HWND editor = CreateWindowW(TINTA_TEXT_EDITOR_CLASSW, input.c_str(),
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_WANTRETURN,
        0, 0, 640, 160, parent, nullptr, instance, nullptr);
    if (!parent || !editor) return 1;
    SendMessageW(editor, WM_PAINT, 0, 0);

    std::atomic<bool> done{false};
    bool result = false;
    HRESULT failed_call = S_OK;
    std::thread client([&] {
        HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IUIAutomation *automation = nullptr;
        IUIAutomationElement *element = nullptr;
        IUIAutomationTextPattern *text_pattern = nullptr;
        IUIAutomationTextRange *document = nullptr;
        IUIAutomationValuePattern *value_pattern = nullptr;
        IUIAutomationScrollPattern *scroll_pattern = nullptr;
        BSTR text = nullptr;
        BSTR value = nullptr;
        CONTROLTYPEID type = 0;
        HRESULT call = CoCreateInstance(CLSID_CUIAutomation, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
        if (SUCCEEDED(call)) call = automation->ElementFromHandle(editor, &element);
        if (SUCCEEDED(call)) call = element->get_CurrentControlType(&type);
        if (SUCCEEDED(call)) call = element->GetCurrentPatternAs(
            UIA_TextPatternId, IID_PPV_ARGS(&text_pattern));
        if (SUCCEEDED(call)) call = text_pattern->get_DocumentRange(&document);
        if (SUCCEEDED(call)) call = document->GetText(64, &text);
        bool ok = SUCCEEDED(call) && type == UIA_EditControlTypeId && text &&
                  wcsstr(text, L"first\nsecond");
        if (ok) call = element->GetCurrentPatternAs(
            UIA_ValuePatternId, IID_PPV_ARGS(&value_pattern));
        if (SUCCEEDED(call)) call = value_pattern->get_CurrentValue(&value);
        ok = ok && SUCCEEDED(call) && value && wcsstr(value, L"second 😀");
        SysFreeString(value);
        value = nullptr;
        if (ok) call = value_pattern->SetValue(L"changed\r\nvalue");
        if (SUCCEEDED(call)) {
            SendMessageW(editor, EM_SETREADONLY, TRUE, 0);
            const HRESULT read_only_call = value_pattern->SetValue(L"blocked");
            ok = ok && read_only_call == UIA_E_INVALIDOPERATION;
            SendMessageW(editor, EM_SETREADONLY, FALSE, 0);
        }
        if (ok) call = element->GetCurrentPatternAs(
            UIA_ScrollPatternId, IID_PPV_ARGS(&scroll_pattern));
        if (SUCCEEDED(call)) call = scroll_pattern->Scroll(
            ScrollAmount_NoAmount, ScrollAmount_SmallIncrement);
        ok = ok && SUCCEEDED(call);
        if (!ok) failed_call = call;
        SysFreeString(text);
        if (scroll_pattern) scroll_pattern->Release();
        if (value_pattern) value_pattern->Release();
        if (document) document->Release();
        if (text_pattern) text_pattern->Release();
        if (element) element->Release();
        if (automation) automation->Release();
        if (SUCCEEDED(initialized)) CoUninitialize();
        result = ok;
        done = true;
    });

    ULONGLONG deadline = GetTickCount64() + 5000;
    while (!done && GetTickCount64() < deadline) {
        pump_messages();
        Sleep(1);
    }
    client.join();
    result = result && done;
    if (result) {
        wchar_t text[64]{};
        SendMessageW(editor, WM_GETTEXT, ARRAYSIZE(text),
                     reinterpret_cast<LPARAM>(text));
        result = wcscmp(text, L"changed\nvalue") == 0;
    }
    DestroyWindow(parent);
    TintaCoreUninitialize();
    if (!result) {
        std::cerr << "editor UIA test failed hr=0x" << std::hex
                  << static_cast<unsigned long>(failed_call) << '\n';
        return 1;
    }
    return 0;
}
