#include "tinta_core.h"

#include <stdbool.h>
#include <windows.h>

#define ID_EDITOR 100
#define ID_THEME  101
#define ID_WRAP   102
#define ID_READONLY 103

static HWND g_editor;
static bool g_dark;
static bool g_wrap = true;
static bool g_read_only;

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            HINSTANCE instance = (HINSTANCE)GetWindowLongPtrW(
                hwnd, GWLP_HINSTANCE);
            CreateWindowW(L"BUTTON", L"Theme", WS_CHILD | WS_VISIBLE,
                8, 8, 80, 28, hwnd, (HMENU)ID_THEME, instance, NULL);
            CreateWindowW(L"BUTTON", L"Wrap", WS_CHILD | WS_VISIBLE,
                96, 8, 80, 28, hwnd, (HMENU)ID_WRAP, instance, NULL);
            CreateWindowW(L"BUTTON", L"Read only", WS_CHILD | WS_VISIBLE,
                184, 8, 96, 28, hwnd, (HMENU)ID_READONLY, instance, NULL);
            g_editor = CreateWindowExW(WS_EX_CLIENTEDGE,
                TINTA_TEXT_EDITOR_CLASSW,
                L"Tinta.TextEditor\n\n"
                L"Uses LF newlines, DirectWrite color emoji 😀 and overlay "
                L"scrollbars.\nTriple-click selects a logical line.\n",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                ES_WANTRETURN,
                8, 44, 760, 500, hwnd, (HMENU)ID_EDITOR, instance, NULL);
            return g_editor ? 0 : -1;
        }
        case WM_SIZE:
            if (g_editor)
                MoveWindow(g_editor, 8, 44, max(1, LOWORD(lparam) - 16),
                           max(1, HIWORD(lparam) - 52), TRUE);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wparam) == ID_THEME) {
                g_dark = !g_dark;
                SendMessageW(g_editor, TEM_SETBUILTINTHEME,
                    g_dark ? TINTA_THEME_MIDNIGHT : TINTA_THEME_SYSTEM, 0);
            } else if (LOWORD(wparam) == ID_WRAP) {
                g_wrap = !g_wrap;
                SendMessageW(g_editor, TEM_SETWORDWRAP,
                    g_wrap ? TINTA_EDITOR_WRAP_ON : TINTA_EDITOR_WRAP_OFF, 0);
            } else if (LOWORD(wparam) == ID_READONLY) {
                g_read_only = !g_read_only;
                SendMessageW(g_editor, EM_SETREADONLY, g_read_only, 0);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show) {
    WNDCLASSW window_class;
    HWND window;
    MSG message;
    (void)previous;
    (void)command_line;
    SetProcessDPIAware();
    if (FAILED(TintaCoreInitialize())) return 1;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    window_class.lpszClassName = L"TintaEditorExample";
    if (!RegisterClassW(&window_class)) {
        TintaCoreUninitialize();
        return 1;
    }
    window = CreateWindowW(window_class.lpszClassName, L"Tinta Text Editor",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, instance, NULL);
    if (!window) {
        TintaCoreUninitialize();
        return 1;
    }
    ShowWindow(window, show);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    TintaCoreUninitialize();
    return (int)message.wParam;
}
