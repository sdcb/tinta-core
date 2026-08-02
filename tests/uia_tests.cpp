#include "tinta_core.h"

#include <UIAutomation.h>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <windows.h>

static int link_notifications;

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    if (message == WM_NOTIFY) {
        NMHDR *header = reinterpret_cast<NMHDR *>(lparam);
        if (header && header->code == TMN_LINKACTIVATE) {
            link_notifications++;
            return TRUE;
        }
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void pump_messages(void) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

int main() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = parent_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"TintaUiaTestParent";
    if (FAILED(TintaCoreInitialize()) || !RegisterClassW(&window_class))
        return 1;
    HWND parent = CreateWindowW(window_class.lpszClassName, L"uia",
        WS_OVERLAPPEDWINDOW, 0, 0, 640, 320, nullptr, nullptr, instance, nullptr);
    HWND view = CreateWindowW(TINTA_MARKDOWN_VIEW_CLASSW,
        L"# First revision\n\ninitial text\n", WS_CHILD | WS_VISIBLE,
        0, 0, 640, 320, parent, nullptr, instance, nullptr);
    if (!parent || !view) return 1;
    SendMessageW(view, WM_PAINT, 0, 0);

    std::atomic<int> phase{0};
    std::atomic<bool> done{false};
    std::atomic<bool> scroll_ok{false};
    bool result = false;
    HRESULT final_call = S_OK;
    HRESULT first_error = S_OK;
    int failure_stage = 0;
    std::thread client([&] {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IUIAutomation *automation = nullptr;
        IUIAutomationElement *element = nullptr;
        IUIAutomationTextPattern *text_pattern = nullptr;
        IUIAutomationTextRange *old_range = nullptr;
        IUIAutomationTextRange *new_range = nullptr;
        IUIAutomationScrollPattern *scroll = nullptr;
        IUIAutomationCondition *link_condition = nullptr;
        IUIAutomationElement *link = nullptr;
        IUIAutomationInvokePattern *invoke = nullptr;
        BSTR text = nullptr;
        CONTROLTYPEID type = 0;
        HRESULT call = CoCreateInstance(CLSID_CUIAutomation, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
        if (SUCCEEDED(call)) call = automation->ElementFromHandle(view, &element);
        if (SUCCEEDED(call)) call = element->get_CurrentControlType(&type);
        if (SUCCEEDED(call)) call = element->GetCurrentPatternAs(
            UIA_TextPatternId, IID_PPV_ARGS(&text_pattern));
        if (SUCCEEDED(call)) call = text_pattern->get_DocumentRange(&old_range);
        if (SUCCEEDED(call)) call = old_range->GetText(-1, &text);
        bool ok = SUCCEEDED(call) && type == UIA_DocumentControlTypeId &&
                  text && wcsstr(text, L"First revision");
        if (!ok) { failure_stage = 1; first_error = call; }
        SysFreeString(text);
        text = nullptr;
        phase = 1;
        while (phase.load() < 2) Sleep(1);
        call = old_range ? old_range->GetText(-1, &text) : E_FAIL;
        ok = ok && call == UIA_E_ELEMENTNOTAVAILABLE;
        if (!ok && !failure_stage) failure_stage = 2;
        SysFreeString(text);
        text = nullptr;
        if (ok) call = text_pattern->get_DocumentRange(&new_range);
        if (SUCCEEDED(call)) call = new_range->GetText(-1, &text);
        ok = ok && SUCCEEDED(call) && text && wcsstr(text, L"Second revision");
        if (!ok && !failure_stage) failure_stage = 3;
        SysFreeString(text);
        text = nullptr;
        if (ok) call = element->GetCurrentPatternAs(
            UIA_ScrollPatternId, IID_PPV_ARGS(&scroll));
        if (SUCCEEDED(call))
            call = scroll->Scroll(ScrollAmount_NoAmount,
                                  ScrollAmount_SmallIncrement);
        scroll_ok = SUCCEEDED(call);
        VARIANT link_type;
        VariantInit(&link_type);
        V_VT(&link_type) = VT_I4;
        V_I4(&link_type) = UIA_HyperlinkControlTypeId;
        if (SUCCEEDED(call)) call = automation->CreatePropertyCondition(
            UIA_ControlTypePropertyId, link_type, &link_condition);
        if (SUCCEEDED(call)) call = element->FindFirst(
            TreeScope_Descendants, link_condition, &link);
        if (SUCCEEDED(call) && link) call = link->GetCurrentPatternAs(
            UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
        if (SUCCEEDED(call) && invoke) call = invoke->Invoke();
        ok = ok && SUCCEEDED(call) && link != nullptr;
        if (!ok && !failure_stage) failure_stage = 4;
        phase = 3;
        for (int iteration = 0; ok && iteration < 2; iteration++) {
            IUIAutomationTextRange *current = nullptr;
            BSTR current_text = nullptr;
            HRESULT stress = text_pattern->get_DocumentRange(&current);
            if (SUCCEEDED(stress)) stress = current->GetText(64, &current_text);
            if (stress != UIA_E_ELEMENTNOTAVAILABLE && FAILED(stress)) {
                ok = false;
                failure_stage = 5;
                first_error = stress;
            }
            SysFreeString(current_text);
            if (current) current->Release();
            Sleep(1);
        }
        if (invoke) invoke->Release();
        if (link) link->Release();
        if (link_condition) link_condition->Release();
        if (scroll) scroll->Release();
        if (new_range) new_range->Release();
        if (old_range) old_range->Release();
        if (text_pattern) text_pattern->Release();
        if (element) element->Release();
        if (automation) automation->Release();
        if (SUCCEEDED(hr)) CoUninitialize();
        result = ok;
        final_call = call;
        done = true;
    });

    ULONGLONG deadline = GetTickCount64() + 5000;
    int stress_revision = 0;
    while (!done && GetTickCount64() < deadline) {
        pump_messages();
        if (phase.load() == 1) {
            std::wstring document =
                L"# Second revision\n\n[link](https://example.com)\n\n";
            for (int index = 0; index < 100; index++)
                document += L"scrolling content\n\n";
            SendMessageW(view, WM_SETTEXT, 0,
                         reinterpret_cast<LPARAM>(document.c_str()));
            SendMessageW(view, WM_PAINT, 0, 0);
            phase = 2;
        } else if (phase.load() == 3 && stress_revision < 2) {
            std::wstring document = L"# Stress revision " +
                std::to_wstring(++stress_revision) +
                L"\n\n[link](https://example.com)\n\nbody\n";
            SendMessageW(view, WM_SETTEXT, 0,
                         reinterpret_cast<LPARAM>(document.c_str()));
        }
        Sleep(1);
    }
    if (!done) phase = 2;
    client.join();
    pump_messages();
    TintaScrollPosition position{};
    position.cb_size = sizeof(position);
    SendMessageW(view, TMM_GETSCROLLPOS, 0,
                 reinterpret_cast<LPARAM>(&position));
    result = result && done && link_notifications == 1 && scroll_ok;
    DestroyWindow(parent);
    TintaCoreUninitialize();
    if (!result) {
        std::cerr << "UI Automation cross-thread/revision test failed stage="
                  << failure_stage << " hr=0x" << std::hex
                  << static_cast<unsigned long>(final_call)
                  << " first=0x" << static_cast<unsigned long>(first_error)
                  << " links=" << std::dec << link_notifications
                  << " scroll=" << position.y << "\n";
        return 1;
    }
    std::cout << "UI Automation tests passed\n";
    return 0;
}
