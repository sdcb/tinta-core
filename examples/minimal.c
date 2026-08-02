#include "tinta_core.h"

#include <stdio.h>
#include <windows.h>

static HWND g_view;

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            g_view = CreateWindowExW(
                0, TINTA_MARKDOWN_VIEW_CLASSW,
                L"# Tinta Core\n\nThis Markdown was supplied with `WM_SETTEXT`.\n\n"
                L"- Select and copy text\n- Use Ctrl+mouse wheel to zoom\n"
                L"- Visit [OpenAI](https://openai.com)",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0, 0, 1, 1, hwnd, (HMENU)100, GetModuleHandleW(NULL), NULL);
            return g_view ? 0 : -1;
        case WM_SIZE:
            if (g_view) MoveWindow(g_view, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
            return 0;
        case WM_NOTIFY: {
            NMHDR *header = (NMHDR *)lparam;
            if (header && header->code == TMN_LINKACTIVATE) {
                const TintaLinkNotify *link = (const TintaLinkNotify *)header;
                wchar_t message_text[1024];
                _snwprintf_s(message_text, _countof(message_text), _TRUNCATE,
                    L"The host intercepted this link:\n\n%s", link->uri);
                MessageBoxW(hwnd, message_text, L"Tinta Core", MB_OK);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show) {
    WNDCLASSW window_class = {0};
    HWND window;
    MSG message;
    (void)previous;
    (void)command_line;
    SetProcessDPIAware();
    if (FAILED(TintaCoreInitialize())) return 1;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.lpszClassName = L"TintaMinimalHost";
    if (!RegisterClassW(&window_class)) return 1;
    window = CreateWindowExW(0, window_class.lpszClassName, L"Tinta Core minimal",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 600,
        NULL, NULL, instance, NULL);
    if (!window) return 1;
    ShowWindow(window, show);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    TintaCoreUninitialize();
    return (int)message.wParam;
}
