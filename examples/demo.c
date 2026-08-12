#include "tinta_core.h"

#include <commctrl.h>
#include <commdlg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#ifdef _MSC_VER
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' " \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' " \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
    "language='*'\"")
#endif

enum {
    ID_OPEN = 100, ID_SEARCH = 101, ID_NEXT = 102, ID_THEME = 103,
    ID_CUSTOM = 104, ID_TOC = 105, ID_VIEW = 200, ID_STATUS = 201,
    ID_TOOLBAR = 202
};

static HWND g_view;
static HWND g_search;
static HWND g_toc;
static HWND g_status;
static HWND g_toolbar;
static HFONT g_ui_font;
static bool g_ui_font_owned;
static int g_theme;

static const char DEFAULT_MARKDOWN[] =
    "# Tinta Core Feature Tour\n\n"
    "> This built-in document exercises the renderer with a little more than "
    "a heading and a sentence.\n\n"
    "## What is included\n\n"
    "- **Rich text** with *emphasis*, `inline code`, and a [link](https://github.com/sdcb/tinta-core).\n"
    "- Nested styles keep their combined faces: **bold `code`, punctuation**, "
    "*italic `code`*, and [linked `code`](https://github.com/sdcb/tinta-core).\n"
    "- Checklists for selection and copy behavior:\n"
    "  - [x] Parse Markdown\n"
    "  - [x] Render code and tables\n"
    "  - [x] Render inline and display math\n"
    "  - [ ] Try Ctrl+mouse wheel to zoom\n\n"
    "## Mathematics\n\n"
    "Inline formulas share the surrounding baseline, for example "
    "$E = mc^2$ and \\(a^2+b^2=c^2\\).\n\n"
    "$$\\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}$$\n\n"
    "\\[\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}"
    "\\begin{pmatrix}x\\\\y\\end{pmatrix}="
    "\\begin{pmatrix}ax+by\\\\cx+dy\\end{pmatrix}\\]\n\n"
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
    "button change to **Copied**, or click elsewhere in the header to "
    "collapse and expand it.\n\n"
    "## Remote image\n\n"
    "This image is fetched asynchronously from the network when remote images "
    "are enabled:\n\n"
    "![Remote Tinta Core demo image](https://placehold.co/640x240/png?text=Tinta+Core+Remote+Image)\n\n"
    "## Mermaid diagram\n\n"
    "```mermaid\n"
    "flowchart LR\n"
    "    A[Markdown source] --> pipeline --> F[Win32 control]\n"
    "    subgraph pipeline [Tinta Core pipeline]\n"
    "        direction TB\n"
    "        B{Feature} --> renderers\n"
    "        subgraph renderers [Renderers]\n"
    "            direction RL\n"
    "            C{{Code and table}}\n"
    "            D((Remote image))\n"
    "            E[Mermaid renderer]\n"
    "        end\n"
    "        B --> C\n"
    "        B --> D\n"
    "        B --> E\n"
    "    end\n"
    "    C --> F\n"
    "    D --> F\n"
    "    E --> F\n"
    "```\n\n"
    "Mermaid uses the same animated header collapse and source Copy behavior.\n\n"
    "## SVG document\n\n"
    "![Tinta vector demo](data:image/svg+xml,%3Csvg%20xmlns%3D%27http%3A//www.w3.org/2000/svg%27%20width%3D%27560%27%20height%3D%27160%27%20viewBox%3D%270%200%20560%20160%27%3E%3Crect%20x%3D%271%27%20y%3D%271%27%20width%3D%27558%27%20height%3D%27158%27%20rx%3D%2720%27%20fill%3D%27%23142538%27%20stroke%3D%27%2306b6d4%27%20stroke-width%3D%272%27/%3E%3Ccircle%20cx%3D%2790%27%20cy%3D%2780%27%20r%3D%2742%27%20fill%3D%27%230ea5e9%27/%3E%3Cpath%20d%3D%27M75%2080l12%2012%2024-28%27%20fill%3D%27none%27%20stroke%3D%27white%27%20stroke-width%3D%278%27/%3E%3Ctext%20x%3D%27160%27%20y%3D%2792%27%20font-size%3D%2734%27%20font-family%3D%27Segoe%20UI%27%20fill%3D%27white%27%3EDirect2D%20SVG%3C/text%3E%3C/svg%3E)\n\n"
    "Standalone SVG images use a persistent **SVG** header with source Copy "
    "and animated collapse. Inline SVG images keep normal image behavior.\n\n"
    "## HTML extensions\n\n"
    "Inline HTML scripts render naturally: H<sub>2</sub>O and x<sup>2</sup>. "
    "They also preserve **<sub>nested emphasis</sub>** and links.\n\n"
    "<details>\n"
    "<summary>Open a Markdown disclosure</summary>\n\n"
    "Details can contain ordinary **Markdown**, lists, images, Code, Mermaid, "
    "SVG, and nested Details. Search automatically opens hidden ancestors.\n\n"
    "```c\n"
    "puts(\"Details body\");\n"
    "```\n\n"
    "<details open>\n"
    "<summary>Nested detail (open by default)</summary>\n\n"
    "The `open` attribute controls the initial state.\n\n"
    "</details>\n\n"
    "</details>\n\n"
    "Scroll through the document, use the table of contents, and open another "
    "Markdown file with **Open** when you want to try your own content.";

static void set_status(const wchar_t *text) {
    if (g_status)
        SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(text ? text : L""));
}

static void populate_toc(void) {
    LRESULT count;
    size_t index;
    HTREEITEM parents[6] = {0};
    TreeView_DeleteAllItems(g_toc);
    count = SendMessageW(g_view, TMM_GETHEADINGCOUNT, 0, 0);
    for (index = 0; index < (size_t)count; index++) {
        wchar_t text[256];
        wchar_t anchor[256];
        TintaHeadingInfo info;
        TVINSERTSTRUCTW insert;
        HTREEITEM item;
        unsigned int level;
        memset(&info, 0, sizeof(info));
        info.cb_size = sizeof(info);
        info.index = index;
        info.text = text;
        info.text_capacity = _countof(text);
        info.anchor = anchor;
        info.anchor_capacity = _countof(anchor);
        if (!SendMessageW(g_view, TMM_GETHEADING, 0, (LPARAM)&info))
            continue;
        level = info.level >= 1 && info.level <= 6 ? info.level : 1;
        memset(&insert, 0, sizeof(insert));
        insert.hParent = level > 1 && parents[level - 2] ?
            parents[level - 2] : TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = text;
        insert.item.lParam = (LPARAM)index;
        item = TreeView_InsertItem(g_toc, &insert);
        if (item) {
            unsigned int deeper;
            parents[level - 1] = item;
            for (deeper = level; deeper < _countof(parents); deeper++)
                parents[deeper] = NULL;
            if (insert.hParent != TVI_ROOT)
                TreeView_Expand(g_toc, insert.hParent, TVE_EXPAND);
        }
    }
}

static bool create_toolbar(HWND parent) {
    TBBUTTON buttons[5];
    UINT dpi = GetDpiForWindow(parent);
    size_t index;
    static const int commands[] = {
        ID_OPEN, ID_SEARCH, ID_NEXT, ID_THEME, ID_CUSTOM
    };
    static const wchar_t *labels[] = {
        L"Open", NULL, L"Find", L"Theme", L"Custom"
    };
    g_toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
        TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
        CCS_TOP | CCS_NODIVIDER,
        0, 0, 0, 0, parent, (HMENU)ID_TOOLBAR, GetModuleHandleW(NULL), NULL);
    if (!g_toolbar) return false;
    SendMessageW(g_toolbar, WM_SETFONT, (WPARAM)g_ui_font, FALSE);
    SendMessageW(g_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(g_toolbar, TB_SETEXTENDEDSTYLE, 0,
                 TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER);
    SendMessageW(g_toolbar, TB_SETBUTTONSIZE, 0,
                 MAKELPARAM(MulDiv(24, dpi, 96), MulDiv(28, dpi, 96)));
    memset(buttons, 0, sizeof(buttons));
    for (index = 0; index < _countof(buttons); index++) {
        buttons[index].iBitmap = index == 1 ? MulDiv(220, dpi, 96) :
                                             I_IMAGENONE;
        buttons[index].idCommand = commands[index];
        buttons[index].fsState = TBSTATE_ENABLED;
        buttons[index].fsStyle = index == 1 ? BTNS_SEP :
            BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        buttons[index].iString = (INT_PTR)labels[index];
    }
    if (!SendMessageW(g_toolbar, TB_ADDBUTTONS, _countof(buttons),
                      (LPARAM)buttons))
        return false;
    SendMessageW(g_toolbar, TB_AUTOSIZE, 0, 0);
    return true;
}

static int child_height(HWND child) {
    RECT rect;
    if (!child || !GetWindowRect(child, &rect)) return 0;
    return rect.bottom - rect.top;
}

static void layout_children(HWND hwnd) {
    RECT client;
    RECT search_rect;
    int toolbar_height;
    int status_height;
    int content_top;
    int content_height;
    int toc_width;
    GetClientRect(hwnd, &client);
    if (g_toolbar) SendMessageW(g_toolbar, TB_AUTOSIZE, 0, 0);
    if (g_status) SendMessageW(g_status, WM_SIZE, 0, 0);
    toolbar_height = child_height(g_toolbar);
    status_height = child_height(g_status);
    content_top = toolbar_height;
    content_height = max(1, client.bottom - toolbar_height - status_height);
    toc_width = MulDiv(220, GetDpiForWindow(hwnd), 96);
    if (g_search && g_toolbar &&
        SendMessageW(g_toolbar, TB_GETRECT, ID_SEARCH,
                     (LPARAM)&search_rect)) {
        SetWindowPos(g_search, HWND_TOP, search_rect.left + 4,
            search_rect.top + 3, max(1, search_rect.right - search_rect.left - 8),
            max(1, search_rect.bottom - search_rect.top - 6),
            SWP_NOACTIVATE);
    }
    if (g_toc)
        MoveWindow(g_toc, 0, content_top, toc_width, content_height, TRUE);
    if (g_view)
        MoveWindow(g_view, toc_width, content_top,
            max(1, client.right - toc_width), content_height, TRUE);
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
                    TINTA_OPTION_DOCUMENT_COPY_BUTTON |
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
        case WM_CREATE: {
            if (!create_toolbar(hwnd)) return -1;
            g_search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 1, 1,
                g_toolbar, (HMENU)ID_SEARCH, GetModuleHandleW(NULL), NULL);
            g_toc = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS |
                TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                0, 0, 1, 1, hwnd, (HMENU)ID_TOC, GetModuleHandleW(NULL), NULL);
            g_view = CreateWindowExW(0, TINTA_MARKDOWN_VIEW_CLASSW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1,
                hwnd, (HMENU)ID_VIEW, GetModuleHandleW(NULL), NULL);
            g_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"Ready",
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 1, 1,
                hwnd, (HMENU)ID_STATUS, GetModuleHandleW(NULL), NULL);
            if (!g_search || !g_toc || !g_view || !g_status) return -1;
            SendMessageW(g_search, WM_SETFONT, (WPARAM)g_ui_font, TRUE);
            SendMessageW(g_toc, WM_SETFONT, (WPARAM)g_ui_font, TRUE);
            SendMessageW(g_status, WM_SETFONT, (WPARAM)g_ui_font, TRUE);
            load_default_document();
            populate_toc();
            layout_children(hwnd);
            return 0;
        }
        case WM_SIZE:
            layout_children(hwnd);
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
            }
            return 0;
        case WM_NOTIFY: {
            NMHDR *header = (NMHDR *)lparam;
            if (header && header->hwndFrom == g_toc &&
                header->code == TVN_SELCHANGEDW) {
                const NMTREEVIEWW *tree = (const NMTREEVIEWW *)header;
                SendMessageW(g_view, TMM_SCROLLTOHEADING,
                             (WPARAM)tree->itemNew.lParam, 0);
                return TRUE;
            }
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
            if (g_ui_font_owned) {
                DeleteObject(g_ui_font);
            }
            g_ui_font = NULL;
            g_ui_font_owned = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show) {
    INITCOMMONCONTROLSEX common;
    NONCLIENTMETRICSW metrics;
    WNDCLASSW window_class = {0};
    HWND window;
    MSG message;
    (void)previous;
    (void)command_line;
    SetProcessDPIAware();
    memset(&common, 0, sizeof(common));
    common.dwSize = sizeof(common);
    common.dwICC = ICC_BAR_CLASSES | ICC_TREEVIEW_CLASSES;
    if (!InitCommonControlsEx(&common)) return 1;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                              &metrics, 0))
        g_ui_font = CreateFontIndirectW(&metrics.lfMessageFont);
    g_ui_font_owned = g_ui_font != NULL;
    if (!g_ui_font) g_ui_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
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
