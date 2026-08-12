#include "tinta_core.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

namespace {

std::vector<UINT> notifications;

LRESULT CALLBACK parent_proc(HWND hwnd, UINT message,
                             WPARAM wparam, LPARAM lparam) {
    (void)hwnd;
    (void)lparam;
    if (message == WM_COMMAND)
        notifications.push_back(HIWORD(wparam));
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void pump_messages() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

std::wstring window_text(HWND editor) {
    const size_t length = static_cast<size_t>(
        SendMessageW(editor, WM_GETTEXTLENGTH, 0, 0));
    std::wstring text(length + 1, L'\0');
    const LRESULT copied = SendMessageW(editor, WM_GETTEXT,
        static_cast<WPARAM>(text.size()), reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(copied));
    return text;
}

TintaEditorSelection selection(HWND editor) {
    TintaEditorSelection value{};
    value.cb_size = sizeof(value);
    SendMessageW(editor, TEM_GETSELECTION, 0,
                 reinterpret_cast<LPARAM>(&value));
    return value;
}

bool clipboard_text(HWND editor, std::wstring *result) {
    if (!OpenClipboard(editor)) return false;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *text = handle ?
        static_cast<const wchar_t *>(GlobalLock(handle)) : nullptr;
    if (text) *result = text;
    if (text) GlobalUnlock(handle);
    CloseClipboard();
    return text != nullptr;
}

bool check(bool condition, const char *message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW parent_class{};
    parent_class.lpfnWndProc = parent_proc;
    parent_class.hInstance = instance;
    parent_class.lpszClassName = L"TintaEditorTestParent";
    if (!check(SUCCEEDED(TintaCoreInitialize()), "core initialization failed"))
        return 1;
    if (!RegisterClassW(&parent_class) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::cerr << "parent registration failed\n";
        TintaCoreUninitialize();
        return 1;
    }
    HWND parent = CreateWindowW(parent_class.lpszClassName, L"parent",
        WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, instance,
        nullptr);
    HWND editor = CreateWindowExW(WS_EX_CLIENTEDGE, TINTA_TEXT_EDITOR_CLASSW,
        L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_WANTRETURN,
        0, 0, 600, 400, parent, reinterpret_cast<HMENU>(101), instance,
        nullptr);
    bool ok = check(parent && editor, "editor creation failed");
    if (ok) {
        notifications.clear();
        ok &= check(SendMessageW(editor, WM_SETTEXT, 0,
            reinterpret_cast<LPARAM>(L"one\r\ntwo\rthree")) != 0,
            "WM_SETTEXT failed");
        ok &= check(window_text(editor) == L"one\ntwo\nthree",
                    "LF normalization failed");
        ok &= check(SendMessageW(editor, EM_GETLINECOUNT, 0, 0) == 3,
                    "line count failed");
        ok &= check(notifications.size() >= 2 &&
                    notifications[notifications.size() - 2] == EN_UPDATE &&
                    notifications.back() == EN_CHANGE,
                    "notification order failed");

        const wchar_t explicit_text[] = L"alpha\r\nbeta\rgamma";
        TintaEditorText input{};
        input.cb_size = sizeof(input);
        input.text = explicit_text;
        input.text_length = ARRAYSIZE(explicit_text) - 1;
        ok &= check(SendMessageW(editor, TEM_SETTEXTEX, 0,
            reinterpret_cast<LPARAM>(&input)) != 0,
            "TEM_SETTEXTEX failed");
        wchar_t range_buffer[16]{};
        TintaEditorTextRange range{};
        range.cb_size = sizeof(range);
        range.start = 6;
        range.end = 10;
        range.text = range_buffer;
        range.text_capacity = ARRAYSIZE(range_buffer);
        ok &= check(SendMessageW(editor, TEM_GETTEXTRANGE, 0,
                    reinterpret_cast<LPARAM>(&range)) == 4 &&
                    std::wstring(range_buffer) == L"beta",
                    "TEM_GETTEXTRANGE failed");
        const wchar_t invalid[] = {L'a', 0, L'b'};
        input.text = invalid;
        input.text_length = ARRAYSIZE(invalid);
        ok &= check(!SendMessageW(editor, TEM_SETTEXTEX, 0,
                     reinterpret_cast<LPARAM>(&input)) &&
                    window_text(editor) == L"alpha\nbeta\ngamma",
                    "embedded NUL was not rejected atomically");

        TintaEditorSelection requested{};
        requested.cb_size = sizeof(requested);
        requested.anchor = 10;
        requested.caret = 6;
        ok &= check(SendMessageW(editor, TEM_SETSELECTION, 0,
                    reinterpret_cast<LPARAM>(&requested)) != 0,
                    "directional selection set failed");
        TintaEditorSelection selected = selection(editor);
        ok &= check(selected.anchor == 10 && selected.caret == 6,
                    "selection direction was not preserved");
        SendMessageW(editor, EM_REPLACESEL, TRUE,
                     reinterpret_cast<LPARAM>(L"B"));
        ok &= check(window_text(editor) == L"alpha\nB\ngamma",
                    "EM_REPLACESEL failed");
        ok &= check(SendMessageW(editor, EM_CANUNDO, 0, 0) != 0 &&
                    SendMessageW(editor, WM_UNDO, 0, 0) != 0 &&
                    window_text(editor) == L"alpha\nbeta\ngamma",
                    "undo failed");
        ok &= check(SendMessageW(editor, TEM_CANREDO, 0, 0) != 0 &&
                    SendMessageW(editor, TEM_REDO, 0, 0) != 0 &&
                    window_text(editor) == L"alpha\nB\ngamma",
                    "redo failed");

        SendMessageW(editor, WM_SETTEXT, 0,
                     reinterpret_cast<LPARAM>(L"shortcut text"));
        SendMessageW(editor, EM_SETSEL, 0, -1);
        SendMessageW(editor, WM_CHAR, 0x01, 0);
        selected = selection(editor);
        ok &= check(window_text(editor) == L"shortcut text" &&
                    selected.anchor == 0 && selected.caret == 13,
                    "Ctrl+A character cleared the selection");
        SendMessageW(editor, EM_REPLACESEL, TRUE,
                     reinterpret_cast<LPARAM>(L"replacement"));
        ok &= check(SendMessageW(editor, WM_UNDO, 0, 0) != 0,
                    "shortcut undo setup failed");
        SendMessageW(editor, WM_CHAR, 0x1a, 0);
        ok &= check(window_text(editor) == L"shortcut text" &&
                    SendMessageW(editor, TEM_CANREDO, 0, 0) != 0,
                    "Ctrl+Z character changed text or cleared redo");
        ok &= check(SendMessageW(editor, TEM_REDO, 0, 0) != 0 &&
                    window_text(editor) == L"replacement",
                    "redo after Ctrl+Z character failed");
        SendMessageW(editor, WM_CHAR, 0x19, 0);
        ok &= check(window_text(editor) == L"replacement",
                    "Ctrl+Y character changed text");

        SendMessageW(editor, WM_SETTEXT, 0,
                     reinterpret_cast<LPARAM>(L"one\ntwo\nthree"));
        SendMessageW(editor, WM_PAINT, 0, 0);
        pump_messages();
        LRESULT packed = SendMessageW(editor, EM_POSFROMCHAR, 5, 0);
        int x = static_cast<short>(LOWORD(packed));
        int y = static_cast<short>(HIWORD(packed)) + 2;
        LPARAM point = MAKELPARAM(x, y);
        SendMessageW(editor, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(editor, WM_LBUTTONUP, 0, point);
        SendMessageW(editor, WM_LBUTTONDBLCLK, MK_LBUTTON, point);
        SendMessageW(editor, WM_LBUTTONUP, 0, point);
        SendMessageW(editor, WM_LBUTTONDOWN, MK_LBUTTON, point);
        selected = selection(editor);
        ok &= check(selected.anchor == 4 && selected.caret == 8,
                    "triple-click did not select the full logical line");
        std::wstring copied;
        SendMessageW(editor, WM_COPY, 0, 0);
        const bool clipboard_read = clipboard_text(editor, &copied);
        ok &= check(clipboard_read && copied == L"two\n" &&
                    copied.find(L'\r') == std::wstring::npos,
                    "clipboard did not preserve LF selection");
        SendMessageW(editor, WM_LBUTTONUP, 0, point);

        SendMessageW(editor, WM_LBUTTONDOWN, MK_LBUTTON, point);
        selected = selection(editor);
        SendMessageW(editor, WM_MOUSEMOVE, 0, MAKELPARAM(599, 399));
        TintaEditorSelection after_move = selection(editor);
        ok &= check(after_move.anchor == selected.anchor &&
                    after_move.caret == selected.caret,
                    "mouse movement without the left button extended selection");
        SendMessageW(editor, WM_LBUTTONUP, 0, MAKELPARAM(599, 399));

        SendMessageW(editor, EM_SETREADONLY, TRUE, 0);
        SendMessageW(editor, EM_SETSEL, 0, -1);
        ok &= check(!SendMessageW(editor, WM_CLEAR, 0, 0) &&
                    window_text(editor) == L"one\ntwo\nthree",
                    "read-only edit was accepted");
        SendMessageW(editor, EM_SETREADONLY, FALSE, 0);

        size_t undo_limit = 1024;
        size_t queried_limit = 0;
        ok &= check(SendMessageW(editor, TEM_SETUNDOLIMIT, 0,
                    reinterpret_cast<LPARAM>(&undo_limit)) != 0 &&
                    SendMessageW(editor, TEM_GETUNDOLIMIT, 0,
                    reinterpret_cast<LPARAM>(&queried_limit)) != 0 &&
                    queried_limit == undo_limit,
                    "undo limit messages failed");
        ok &= check(SendMessageW(editor, TEM_SETWORDWRAP,
                    TINTA_EDITOR_WRAP_OFF, 0) != 0 &&
                    SendMessageW(editor, TEM_GETWORDWRAP, 0, 0) ==
                    TINTA_EDITOR_WRAP_OFF,
                    "wrap messages failed");
    }

    if (editor) DestroyWindow(editor);
    if (parent) DestroyWindow(parent);
    UnregisterClassW(parent_class.lpszClassName, instance);
    TintaCoreUninitialize();
    return ok ? 0 : 1;
}
