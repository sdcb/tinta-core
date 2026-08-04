#include "tinta_core.h"

#include <commdlg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum {
    ID_OPEN = 100, ID_SEARCH = 101, ID_NEXT = 102, ID_THEME = 103,
    ID_CUSTOM = 104, ID_TOC = 105, ID_VIEW = 200, ID_STATUS = 201
};

static HWND g_view;
static HWND g_search;
static HWND g_toc;
static HWND g_status;
static int g_theme;

static const char DEFAULT_MARKDOWN[] =
    "# Tinta Core Feature Tour\n\n"
    "> This built-in document exercises the renderer with a little more than "
    "a heading and a sentence.\n\n"
    "## What is included\n\n"
    "- **Rich text** with *emphasis*, `inline code`, and a [link](https://github.com/sdcb/tinta-core).\n"
    "- Checklists for selection and copy behavior:\n"
    "  - [x] Parse Markdown\n"
    "  - [x] Render code and tables\n"
    "  - [ ] Try Ctrl+mouse wheel to zoom\n\n"
    "## A small table\n\n"
    "| Feature | Demonstration | Status |\n"
    "|---|---|---|\n"
    "| Code | Syntax highlighting and copy button | Ready |\n"
    "| Images | Remote image loading and scaling | Online |\n"
    "| Mermaid | Diagram layout and labels | Ready |\n"
    "| Selection | Keyboard and mouse selection | Ready |\n\n"
    "## Code sample\n\n"
    "```c\n"
    "typedef struct DemoMessage {\n"
    "    const char *text;\n"
    "    unsigned int priority;\n"
    "} DemoMessage;\n\n"
    "static void print_message(const DemoMessage *message) {\n"
    "    if (message && message->text)\n"
    "        printf(\"[%u] %s\\n\", message->priority, message->text);\n"
    "}\n"
    "```\n\n"
    "The code block should expose the **Copy** button. Click it to see the "
    "button change to **Copied**.\n\n"
    "## Remote image\n\n"
    "This image is fetched asynchronously from the network when remote images "
    "are enabled:\n\n"
    "![Remote Tinta Core demo image](https://placehold.co/640x240/png?text=Tinta+Core+Remote+Image)\n\n"
    "## Mermaid diagram\n\n"
    "```mermaid\n"
    "flowchart LR\n"
    "    A[Markdown source] --> B{Feature}\n"
    "    B --> C[Code and table]\n"
    "    B --> D[Remote image]\n"
    "    B --> E[Mermaid renderer]\n"
    "    C --> F[Win32 control]\n"
    "    D --> F\n"
    "    E --> F\n"
    "```\n\n"
    "Scroll through the document, use the table of contents, and open another "
    "Markdown file with **Open** when you want to try your own content.";

static void set_status(const wchar_t *text) {
    if (g_status) SetWindowTextW(g_status, text ? text : L"");
}

static void populate_toc(void) {
    LRESULT count;
    size_t index;
    SendMessageW(g_toc, LB_RESETCONTENT, 0, 0);
    count = SendMessageW(g_view, TMM_GETHEADINGCOUNT, 0, 0);
    for (index = 0; index < (size_t)count; index++) {
        wchar_t text[256];
        wchar_t anchor[256];
        TintaHeadingInfo info;
        memset(&info, 0, sizeof(info));
        info.cb_size = sizeof(info);
        info.index = index;
        info.text = text;
        info.text_capacity = _countof(text);
        info.anchor = anchor;
        info.anchor_capacity = _countof(anchor);
        if (SendMessageW(g_view, TMM_GETHEADING, 0, (LPARAM)&info))
            SendMessageW(g_toc, LB_ADDSTRING, 0, (LPARAM)text);
    }
}

static void apply_custom_theme(void) {
    TintaThemeSpec theme;
    memset(&theme, 0, sizeof(theme));
    theme.cb_size = sizeof(theme);
    theme.font_family = L"Segoe UI";
    theme.code_font_family = L"Cascadia Mono";
    theme.background = 0xFFFDF7;
    theme.text = 0x352F2A;
    theme.heading = 0x7C3AED;
    theme.link = 0x2563EB;
    theme.code = 0xB45309;
    theme.code_background = 0xF4EEE8;
    theme.quote = 0xA78BFA;
    theme.accent = 0x7C3AED;
    theme.syntax_keyword = 0x7C3AED;
    theme.syntax_string = 0x15803D;
    theme.syntax_comment = 0x78716C;
    theme.syntax_number = 0xC2410C;
    theme.syntax_function = 0x1D4ED8;
    theme.syntax_type = 0xA21CAF;
    theme.syntax_control = 0xBE123C;
    SendMessageW(g_view, TMM_SETCUSTOMTHEME, 0, (LPARAM)&theme);
}

static void load_default_document(void) {
    TintaDocument document;
    TintaOptions options;
    memset(&options, 0, sizeof(options));
    options.cb_size = sizeof(options);
    options.flags = TINTA_OPTION_SELECTION |
                    TINTA_OPTION_KEYBOARD_NAVIGATION |
                    TINTA_OPTION_MOUSE_ZOOM |
                    TINTA_OPTION_CODE_COPY_BUTTON |
                    TINTA_OPTION_LOCAL_IMAGES |
                    TINTA_OPTION_REMOTE_IMAGES |
                    TINTA_OPTION_OPEN_UNHANDLED_LINKS;
    SendMessageW(g_view, TMM_SETOPTIONS, 0, (LPARAM)&options);

    memset(&document, 0, sizeof(document));
    document.cb_size = sizeof(document);
    document.utf8 = DEFAULT_MARKDOWN;
    document.utf8_length = sizeof(DEFAULT_MARKDOWN) - 1;
    document.format = TINTA_FORMAT_MARKDOWN;
    SendMessageW(g_view, TMM_SETDOCUMENT, 0, (LPARAM)&document);
}

static bool read_file_utf8(const wchar_t *path, char **data, size_t *length) {
    FILE *file = NULL;
    long size;
    char *buffer;
    if (_wfopen_s(&file, path, L"rb") || !file) return false;
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return false;
    }
    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) { fclose(file); return false; }
    if (size && fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer); fclose(file); return false;
    }
    fclose(file);
    buffer[size] = 0;
    *data = buffer;
    *length = (size_t)size;
    return true;
}

static void open_document(HWND owner) {
    OPENFILENAMEW dialog = {0};
    wchar_t path[MAX_PATH] = L"";
    char *data = NULL;
    size_t length = 0;
    TintaDocument document;
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Markdown (*.md;*.markdown;*.mmd)\0*.md;*.markdown;*.mmd\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = _countof(path);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog) || !read_file_utf8(path, &data, &length)) return;
    memset(&document, 0, sizeof(document));
    document.cb_size = sizeof(document);
    document.utf8 = data;
    document.utf8_length = length;
    document.base_uri = path;
    document.format = TINTA_FORMAT_AUTO;
    SendMessageW(g_view, TMM_SETDOCUMENT, 0, (LPARAM)&document);
    free(data);
    SetWindowTextW(owner, path);
}

static void run_search(void) {
    wchar_t query[256];
    TintaFindRequest request;
    GetWindowTextW(g_search, query, _countof(query));
    memset(&request, 0, sizeof(request));
    request.cb_size = sizeof(request);
    request.text = query;
    request.text_length = wcslen(query);
    request.flags = TINTA_FIND_WRAP;
    SendMessageW(g_view, TMM_FIND, 0, (LPARAM)&request);
}

static LRESULT CALLBACK demo_proc(HWND hwnd, UINT message,
                                  WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            CreateWindowW(L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE,
                8, 8, 64, 26, hwnd, (HMENU)ID_OPEN, GetModuleHandleW(NULL), NULL);
            g_search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 80, 8, 220, 26,
                hwnd, (HMENU)ID_SEARCH, GetModuleHandleW(NULL), NULL);
            CreateWindowW(L"BUTTON", L"Find", WS_CHILD | WS_VISIBLE,
                308, 8, 64, 26, hwnd, (HMENU)ID_NEXT, GetModuleHandleW(NULL), NULL);
            CreateWindowW(L"BUTTON", L"Theme", WS_CHILD | WS_VISIBLE,
                380, 8, 70, 26, hwnd, (HMENU)ID_THEME, GetModuleHandleW(NULL), NULL);
            CreateWindowW(L"BUTTON", L"Custom", WS_CHILD | WS_VISIBLE,
                458, 8, 72, 26, hwnd, (HMENU)ID_CUSTOM, GetModuleHandleW(NULL), NULL);
            g_toc = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
                0, 42, 220, 1, hwnd, (HMENU)ID_TOC, GetModuleHandleW(NULL), NULL);
            g_view = CreateWindowExW(0, TINTA_MARKDOWN_VIEW_CLASSW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 220, 42, 1, 1,
                hwnd, (HMENU)ID_VIEW, GetModuleHandleW(NULL), NULL);
            g_status = CreateWindowW(L"STATIC", L"Ready",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 8, 1, 1, 22,
                hwnd, (HMENU)ID_STATUS, GetModuleHandleW(NULL), NULL);
            if (g_view) load_default_document();
            populate_toc();
            return g_view ? 0 : -1;
        case WM_SIZE:
            if (g_toc) MoveWindow(g_toc, 0, 42, 220,
                max(1, HIWORD(lparam) - 66), TRUE);
            if (g_view) MoveWindow(g_view, 220, 42,
                max(1, LOWORD(lparam) - 220), max(1, HIWORD(lparam) - 66), TRUE);
            if (g_status) MoveWindow(g_status, 8, max(42, HIWORD(lparam) - 22),
                max(1, LOWORD(lparam) - 16), 20, TRUE);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wparam) == ID_OPEN) open_document(hwnd);
            else if (LOWORD(wparam) == ID_NEXT) run_search();
            else if (LOWORD(wparam) == ID_THEME) {
                g_theme = (g_theme + 1) % 10;
                SendMessageW(g_view, TMM_SETBUILTINTHEME, g_theme, 0);
                set_status(L"Changed built-in theme");
            } else if (LOWORD(wparam) == ID_CUSTOM) {
                apply_custom_theme();
                set_status(L"Applied custom host theme");
            } else if (LOWORD(wparam) == ID_TOC &&
                       HIWORD(wparam) == LBN_SELCHANGE) {
                LRESULT selected = SendMessageW(g_toc, LB_GETCURSEL, 0, 0);
                if (selected != LB_ERR)
                    SendMessageW(g_view, TMM_SCROLLTOHEADING, selected, 0);
            }
            return 0;
        case WM_NOTIFY: {
            NMHDR *header = (NMHDR *)lparam;
            if (header && header->code == TMN_REQUESTFIND) {
                SetFocus(g_search);
                SendMessageW(g_search, EM_SETSEL, 0, -1);
                return TRUE;
            }
            if (header && header->code == TMN_DOCUMENTREADY) {
                populate_toc();
                set_status(L"Document layout complete");
                return TRUE;
            }
            if (header && header->code == TMN_RESOURCEOPENING) {
                const TintaResourceNotify *resource =
                    (const TintaResourceNotify *)header;
                set_status(resource->resolved_uri);
                return TINTA_RESOURCE_DEFAULT;
            }
            if (header && header->code == TMN_COPYCOMPLETED) {
                set_status(L"Copied to the clipboard");
                return TRUE;
            }
            if (header && header->code == TMN_ERROR) {
                const TintaErrorNotify *error = (const TintaErrorNotify *)header;
                MessageBoxW(hwnd, error->message, error->operation, MB_ICONERROR);
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
    window_class.lpfnWndProc = demo_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    window_class.lpszClassName = L"TintaDemoHost";
    if (!RegisterClassW(&window_class)) return 1;
    window = CreateWindowExW(0, window_class.lpszClassName, L"Tinta Core Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 700,
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
