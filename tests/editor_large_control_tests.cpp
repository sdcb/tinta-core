#include "tinta_core.h"

#include <iostream>
#include <string>
#include <windows.h>

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int main() {
    constexpr size_t target_units = 50u * 1024u * 1024u;
    constexpr size_t target_lines = 1000000u;
    std::wstring text;
    text.reserve(target_units);
    for (size_t line = 0; line < target_lines; line++)
        text += L"01234567890123456789012345678901234567890123456789\n";
    while (text.size() < target_units) text += L'x';

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW parent_class{};
    parent_class.lpfnWndProc = parent_proc;
    parent_class.hInstance = instance;
    parent_class.lpszClassName = L"TintaLargeEditorTestParent";
    if (FAILED(TintaCoreInitialize()) || !RegisterClassW(&parent_class))
        return 1;
    HWND parent = CreateWindowW(parent_class.lpszClassName, L"large",
        WS_OVERLAPPEDWINDOW, 0, 0, 800, 600, nullptr, nullptr, instance,
        nullptr);
    HWND editor = CreateWindowW(TINTA_TEXT_EDITOR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_WANTRETURN,
        0, 0, 800, 600, parent, nullptr, instance, nullptr);
    bool ok = parent && editor;
    TintaEditorText input{};
    input.cb_size = sizeof(input);
    input.text = text.data();
    input.text_length = text.size();
    ok = ok && SendMessageW(editor, TEM_SETTEXTEX, 0,
                            reinterpret_cast<LPARAM>(&input)) != 0;
    ok = ok && static_cast<size_t>(SendMessageW(
        editor, WM_GETTEXTLENGTH, 0, 0)) == text.size();
    ok = ok && static_cast<size_t>(SendMessageW(
        editor, EM_GETLINECOUNT, 0, 0)) == target_lines + 1;

    /* Paint and edit before the posted whole-document measurement completes. */
    SendMessageW(editor, WM_PAINT, 0, 0);
    TintaEditorSelection selection{};
    selection.cb_size = sizeof(selection);
    selection.anchor = selection.caret = text.size() / 2;
    ok = ok && SendMessageW(editor, TEM_SETSELECTION, 0,
                            reinterpret_cast<LPARAM>(&selection)) != 0;
    SendMessageW(editor, EM_REPLACESEL, TRUE,
                 reinterpret_cast<LPARAM>(L"middle\n"));
    ok = ok && SendMessageW(editor, EM_CANUNDO, 0, 0) != 0;
    ok = ok && SendMessageW(editor, WM_UNDO, 0, 0) != 0;
    TintaScrollPosition scroll{};
    scroll.cb_size = sizeof(scroll);
    scroll.y = 1000000.0f;
    ok = ok && SendMessageW(editor, TEM_SETSCROLLPOS, 0,
                            reinterpret_cast<LPARAM>(&scroll)) != 0;
    TintaContentSize content{};
    content.cb_size = sizeof(content);
    ok = ok && SendMessageW(editor, TEM_GETCONTENTSIZE, 0,
                            reinterpret_cast<LPARAM>(&content)) != 0 &&
         content.height > 1000000.0f;

    if (editor) DestroyWindow(editor);
    if (parent) DestroyWindow(parent);
    UnregisterClassW(parent_class.lpszClassName, instance);
    TintaCoreUninitialize();
    if (!ok) {
        std::cerr << "100 MiB editor HWND stress test failed\n";
        return 1;
    }
    return 0;
}
