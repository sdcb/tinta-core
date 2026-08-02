#define WIN32_LEAN_AND_MEAN
#include "tinta_core.h"

#include <commctrl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <windows.h>
#include <windowsx.h>

enum {
    ID_HISTORY = 100,
    ID_INPUT = 101,
    ID_SEND = 102,
    ID_MESSAGE_FIRST = 1000,
    TIMER_STREAM = 1,
    TIMER_REFLOW = 2
};

typedef enum ChatRole {
    CHAT_ROLE_ASSISTANT,
    CHAT_ROLE_USER
} ChatRole;

typedef struct ChatMessage {
    HWND view;
    ChatRole role;
    wchar_t *markdown;
    size_t length;
    size_t capacity;
    int logical_y;
    int width;
    int height;
} ChatMessage;

typedef struct ChatApp {
    HINSTANCE instance;
    HWND main_window;
    HWND history;
    HWND input;
    HWND send;
    HFONT ui_font;
    ChatMessage *messages;
    size_t message_count;
    size_t message_capacity;
    size_t active_message;
    const wchar_t *active_template;
    char *active_utf8;
    size_t active_utf8_length;
    size_t stream_position;
    size_t stream_delta;
    size_t next_template;
    UINT next_control_id;
    int scroll_y;
    int content_height;
    int wheel_remainder;
    bool follow_bottom;
    bool streaming;
    wchar_t stream_base_uri[MAX_PATH * 2];
    wchar_t local_image_path[MAX_PATH * 2];
} ChatApp;

static ChatApp g_app;

static const wchar_t USER_PREFIX[] = L"**You**\n\n";
static const char ASSISTANT_STREAM_PREFIX[] =
    "**Assistant**\n\n"
    "![Bundled local image](chat-stream.bmp)\n\n"
    "![Remote image](https://httpbin.org/image/png)\n\n";

static const wchar_t TEMPLATE_WORD[] =
    L"# Word Explorer: `serendipity`\n\n"
    L"> **Meaning:** the pleasant discovery of something valuable or "
    L"interesting by chance.\n\n"
    L"## Common uses\n\n"
    L"- A lucky discovery while looking for something else.\n"
    L"- An unexpected meeting that leads to a useful idea.\n"
    L"- A fortunate result that was not part of the original plan.\n\n"
    L"## Example sentences\n\n"
    L"1. Finding that quiet bookshop was pure **serendipity**.\n"
    L"2. Their collaboration began through a serendipitous conversation.\n"
    L"3. Good research often leaves room for serendipity.\n\n"
    L"**Usage note:** `Serendipity` is positive and usually describes an "
    L"unexpected discovery rather than ordinary luck.\n\n"
    L"See [Merriam-Webster](https://www.merriam-webster.com/dictionary/serendipity) "
    L"for another definition.";

static const wchar_t TEMPLATE_CODE[] =
    L"# A small C response checklist\n\n"
    L"> [!TIP]\n"
    L"> Keep UI work on the window thread and move slow I/O elsewhere.\n\n"
    L"| Area | Recommendation |\n"
    L"|---|---|\n"
    L"| Input | Validate before starting work |\n"
    L"| Streaming | Append small chunks at a bounded rate |\n"
    L"| Rendering | Keep the latest complete Markdown snapshot |\n"
    L"| Errors | Report them without destroying chat history |\n\n"
    L"```c\n"
    L"static void post_chunk(HWND window, wchar_t *chunk) {\n"
    L"    PostMessageW(window, WM_APP + 1, 0, (LPARAM)chunk);\n"
    L"}\n"
    L"```\n\n"
    L"- [x] Keep ownership explicit\n"
    L"- [x] Update controls on the UI thread\n"
    L"- [ ] Replace the demo timer with a real SSE client";

static const wchar_t TEMPLATE_MERMAID[] =
    L"# Streaming request lifecycle\n\n"
    L"```mermaid\n"
    L"flowchart LR\n"
    L"    A[User input] --> B[Send request]\n"
    L"    B --> C{SSE event}\n"
    L"    C --> D[Append text]\n"
    L"    D --> E[Render Markdown]\n"
    L"    E --> C\n"
    L"    C -->|done| F[Complete response]\n"
    L"```\n\n"
    L"The host owns the request and conversation state. The Markdown control "
    L"only receives snapshots, lays them out, and renders the result.\n\n"
    L"> Keeping those responsibilities separate makes the viewer reusable in "
    L"other Win32 applications.";

static const wchar_t *const RESPONSE_TEMPLATES[] = {
    TEMPLATE_WORD,
    TEMPLATE_CODE,
    TEMPLATE_MERMAID
};

static void put_u16(unsigned char *data, size_t offset, unsigned int value) {
    data[offset] = (unsigned char)value;
    data[offset + 1] = (unsigned char)(value >> 8);
}

static void put_u32(unsigned char *data, size_t offset, unsigned long value) {
    data[offset] = (unsigned char)value;
    data[offset + 1] = (unsigned char)(value >> 8);
    data[offset + 2] = (unsigned char)(value >> 16);
    data[offset + 3] = (unsigned char)(value >> 24);
}

static bool write_all(HANDLE file, const void *data, DWORD length) {
    const unsigned char *bytes = (const unsigned char *)data;
    while (length) {
        DWORD written = 0;
        if (!WriteFile(file, bytes, length, &written, NULL) || !written)
            return false;
        bytes += written;
        length -= written;
    }
    return true;
}

static bool prepare_local_image(void) {
    enum { WIDTH = 96, HEIGHT = 48, ROW_BYTES = WIDTH * 3 };
    wchar_t temporary[MAX_PATH];
    wchar_t directory[MAX_PATH * 2];
    unsigned char header[54] = {0};
    unsigned char row[ROW_BYTES];
    HANDLE file;
    int y;
    if (!GetTempPathW(_countof(temporary), temporary)) return false;
    _snwprintf_s(directory, _countof(directory), _TRUNCATE,
                 L"%sTintaCoreChatDemo", temporary);
    if (!CreateDirectoryW(directory, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    _snwprintf_s(g_app.local_image_path, _countof(g_app.local_image_path),
                 _TRUNCATE, L"%s\\chat-stream.bmp", directory);
    _snwprintf_s(g_app.stream_base_uri, _countof(g_app.stream_base_uri),
                 _TRUNCATE, L"%s\\chat.md", directory);
    file = CreateFileW(g_app.local_image_path, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    header[0] = 'B';
    header[1] = 'M';
    put_u32(header, 2, 54 + ROW_BYTES * HEIGHT);
    put_u32(header, 10, 54);
    put_u32(header, 14, 40);
    put_u32(header, 18, WIDTH);
    put_u32(header, 22, HEIGHT);
    put_u16(header, 26, 1);
    put_u16(header, 28, 24);
    put_u32(header, 34, ROW_BYTES * HEIGHT);
    if (!write_all(file, header, sizeof(header))) {
        CloseHandle(file);
        return false;
    }
    for (y = 0; y < HEIGHT; y++) {
        int x;
        for (x = 0; x < WIDTH; x++) {
            row[x * 3] = (unsigned char)(80 + x);
            row[x * 3 + 1] = (unsigned char)(70 + y * 3);
            row[x * 3 + 2] = (unsigned char)(220 - x);
        }
        if (!write_all(file, row, sizeof(row))) {
            CloseHandle(file);
            return false;
        }
    }
    CloseHandle(file);
    return true;
}

static int scale_value(HWND hwnd, int value) {
    return MulDiv(value, (int)GetDpiForWindow(hwnd), 96);
}

static int maximum_int(int left, int right) {
    return left > right ? left : right;
}

static int minimum_int(int left, int right) {
    return left < right ? left : right;
}

static bool ensure_message_capacity(size_t required) {
    ChatMessage *messages;
    size_t capacity;
    if (required <= g_app.message_capacity) return true;
    capacity = g_app.message_capacity ? g_app.message_capacity * 2 : 16;
    while (capacity < required) capacity *= 2;
    messages = (ChatMessage *)realloc(
        g_app.messages, capacity * sizeof(*messages));
    if (!messages) return false;
    g_app.messages = messages;
    g_app.message_capacity = capacity;
    return true;
}

static bool reserve_markdown(ChatMessage *message, size_t required) {
    wchar_t *markdown;
    size_t capacity;
    if (required <= message->capacity) return true;
    capacity = message->capacity ? message->capacity * 2 : 256;
    while (capacity < required) capacity *= 2;
    markdown = (wchar_t *)realloc(message->markdown,
                                  capacity * sizeof(*markdown));
    if (!markdown) return false;
    message->markdown = markdown;
    message->capacity = capacity;
    return true;
}

static bool assign_markdown(ChatMessage *message, const wchar_t *text) {
    size_t length = text ? wcslen(text) : 0;
    if (!reserve_markdown(message, length + 1)) return false;
    if (length) memcpy(message->markdown, text, length * sizeof(*text));
    message->markdown[length] = 0;
    message->length = length;
    return true;
}

static int history_max_scroll(void) {
    RECT client;
    if (!g_app.history) return 0;
    GetClientRect(g_app.history, &client);
    return maximum_int(0, g_app.content_height - (client.bottom - client.top));
}

static int history_bubble_width(void) {
    RECT client;
    int margin;
    int available;
    int width;
    GetClientRect(g_app.history, &client);
    margin = scale_value(g_app.history, 18);
    available = maximum_int(1, client.right - client.left - margin * 2);
    width = MulDiv(client.right - client.left, 72, 100);
    width = minimum_int(width, available);
    if (available >= scale_value(g_app.history, 320))
        width = maximum_int(width, scale_value(g_app.history, 320));
    return maximum_int(1, width);
}

static void update_round_region(ChatMessage *message) {
    int radius;
    HRGN region;
    if (!message || !message->view) return;
    radius = scale_value(message->view, 18);
    region = CreateRoundRectRgn(0, 0, message->width + 1,
                                message->height + 1, radius, radius);
    if (!region) return;
    if (!SetWindowRgn(message->view, region, TRUE)) DeleteObject(region);
}

static void position_history_children(void) {
    RECT client;
    HDWP batch;
    size_t index;
    int margin;
    GetClientRect(g_app.history, &client);
    margin = scale_value(g_app.history, 18);
    batch = BeginDeferWindowPos((int)g_app.message_count);
    for (index = 0; index < g_app.message_count; index++) {
        ChatMessage *message = &g_app.messages[index];
        int x = message->role == CHAT_ROLE_ASSISTANT ? margin :
            client.right - margin - message->width;
        int y = message->logical_y - g_app.scroll_y;
        if (batch) {
            batch = DeferWindowPos(batch, message->view, NULL, x, y,
                message->width, message->height,
                SWP_NOACTIVATE | SWP_NOZORDER);
        } else {
            SetWindowPos(message->view, NULL, x, y,
                message->width, message->height,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }
    }
    if (batch) EndDeferWindowPos(batch);
}

static void update_history_scrollbar(void) {
    RECT client;
    SCROLLINFO info;
    GetClientRect(g_app.history, &client);
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = maximum_int(0, g_app.content_height - 1);
    info.nPage = (UINT)maximum_int(0, client.bottom - client.top);
    info.nPos = g_app.scroll_y;
    SetScrollInfo(g_app.history, SB_VERT, &info, TRUE);
}

static void layout_history(bool force_bottom) {
    size_t index;
    int y = scale_value(g_app.history, 18);
    int gap = scale_value(g_app.history, 12);
    int maximum;
    for (index = 0; index < g_app.message_count; index++) {
        g_app.messages[index].logical_y = y;
        y += g_app.messages[index].height + gap;
    }
    g_app.content_height = y + scale_value(g_app.history, 6);
    maximum = history_max_scroll();
    if (force_bottom) g_app.follow_bottom = true;
    if (g_app.follow_bottom) g_app.scroll_y = maximum;
    else g_app.scroll_y = minimum_int(maximum_int(0, g_app.scroll_y), maximum);
    update_history_scrollbar();
    position_history_children();
    InvalidateRect(g_app.history, NULL, TRUE);
}

static void set_history_scroll(int position, bool user_action) {
    int maximum = history_max_scroll();
    int threshold = scale_value(g_app.history, 24);
    g_app.scroll_y = minimum_int(maximum_int(0, position), maximum);
    if (user_action)
        g_app.follow_bottom = g_app.scroll_y >= maximum - threshold;
    update_history_scrollbar();
    position_history_children();
    InvalidateRect(g_app.history, NULL, TRUE);
}

static ChatMessage *find_message(HWND view) {
    size_t index;
    for (index = 0; index < g_app.message_count; index++) {
        if (g_app.messages[index].view == view) return &g_app.messages[index];
    }
    return NULL;
}

static void apply_message_theme(HWND view, ChatRole role) {
    TintaThemeSpec theme;
    memset(&theme, 0, sizeof(theme));
    theme.cb_size = sizeof(theme);
    theme.font_family = L"Segoe UI";
    theme.code_font_family = L"Cascadia Mono";
    if (role == CHAT_ROLE_USER) {
        theme.dark = TRUE;
        theme.background = 0x2563EB;
        theme.text = 0xFFFFFF;
        theme.heading = 0xFFFFFF;
        theme.link = 0xDBEAFE;
        theme.code = 0xFFFFFF;
        theme.code_background = 0x1D4ED8;
        theme.quote = 0xBFDBFE;
        theme.accent = 0x93C5FD;
        theme.syntax_keyword = 0xFDE68A;
        theme.syntax_string = 0xBBF7D0;
        theme.syntax_comment = 0xBFDBFE;
        theme.syntax_number = 0xFED7AA;
        theme.syntax_function = 0xE9D5FF;
        theme.syntax_type = 0xFBCFE8;
        theme.syntax_control = 0xFDE68A;
    } else {
        theme.dark = FALSE;
        theme.background = 0xF3F4F6;
        theme.text = 0x1F2937;
        theme.heading = 0x111827;
        theme.link = 0x2563EB;
        theme.code = 0x7C2D12;
        theme.code_background = 0xE5E7EB;
        theme.quote = 0x6B7280;
        theme.accent = 0x7C3AED;
        theme.syntax_keyword = 0x7C3AED;
        theme.syntax_string = 0x15803D;
        theme.syntax_comment = 0x6B7280;
        theme.syntax_number = 0xC2410C;
        theme.syntax_function = 0x1D4ED8;
        theme.syntax_type = 0xA21CAF;
        theme.syntax_control = 0xBE123C;
    }
    SendMessageW(view, TMM_SETCUSTOMTHEME, 0, (LPARAM)&theme);
}

static void configure_message_options(HWND view) {
    TintaOptions options;
    memset(&options, 0, sizeof(options));
    options.cb_size = sizeof(options);
    if (!SendMessageW(view, TMM_GETOPTIONS, 0, (LPARAM)&options)) return;
    options.flags &= ~TINTA_OPTION_MOUSE_ZOOM;
    SendMessageW(view, TMM_SETOPTIONS, 0, (LPARAM)&options);
}

static void configure_message_auto_size(HWND view) {
    TintaAutoSize auto_size;
    memset(&auto_size, 0, sizeof(auto_size));
    auto_size.cb_size = sizeof(auto_size);
    auto_size.flags = TINTA_AUTOSIZE_HEIGHT |
                      TINTA_AUTOSIZE_MAX_HEIGHT;
    auto_size.min_height = 68.0f;
    auto_size.max_height = 440.0f;
    SendMessageW(view, TMM_SETAUTOSIZE, 0, (LPARAM)&auto_size);
}

static bool message_can_scroll_wheel(HWND hwnd, int delta) {
    TintaContentSize content;
    TintaScrollPosition position;
    RECT client;
    float maximum;
    memset(&content, 0, sizeof(content));
    memset(&position, 0, sizeof(position));
    content.cb_size = sizeof(content);
    position.cb_size = sizeof(position);
    if (!GetClientRect(hwnd, &client) ||
        !SendMessageW(hwnd, TMM_GETCONTENTSIZE, 0, (LPARAM)&content) ||
        !SendMessageW(hwnd, TMM_GETSCROLLPOS, 0, (LPARAM)&position))
        return false;
    maximum = content.height - (client.bottom - client.top);
    if (maximum <= 0) return false;
    if (delta > 0) return position.y > 0.5f;
    if (delta < 0) return position.y < maximum - 0.5f;
    return true;
}

static LRESULT CALLBACK markdown_subclass(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam, UINT_PTR subclass_id, DWORD_PTR reference) {
    HWND history = (HWND)reference;
    (void)subclass_id;
    if (message == WM_MOUSEWHEEL &&
        message_can_scroll_wheel(hwnd, GET_WHEEL_DELTA_WPARAM(wparam)))
        return DefSubclassProc(hwnd, message, wparam, lparam);
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
        return SendMessageW(history, message, wparam, lparam);
    if (message == WM_KEYDOWN && !((GetKeyState(VK_CONTROL) & 0x8000) != 0)) {
        if (wparam == VK_PRIOR || wparam == VK_NEXT ||
            wparam == VK_HOME || wparam == VK_END)
            return SendMessageW(history, message, wparam, lparam);
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, markdown_subclass, 1);
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static ChatMessage *create_message(ChatRole role, const wchar_t *markdown) {
    ChatMessage *message;
    HWND view;
    int width;
    int height;
    if (!ensure_message_capacity(g_app.message_count + 1)) return NULL;
    width = history_bubble_width();
    height = scale_value(g_app.history, 84);
    view = CreateWindowExW(0, TINTA_MARKDOWN_VIEW_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, width, height,
        g_app.history, (HMENU)(UINT_PTR)g_app.next_control_id++,
        g_app.instance, NULL);
    if (!view) return NULL;
    message = &g_app.messages[g_app.message_count];
    memset(message, 0, sizeof(*message));
    message->view = view;
    message->role = role;
    message->width = width;
    message->height = height;
    if (!assign_markdown(message, markdown)) {
        DestroyWindow(view);
        memset(message, 0, sizeof(*message));
        return NULL;
    }
    g_app.message_count++;
    SetWindowSubclass(view, markdown_subclass, 1, (DWORD_PTR)g_app.history);
    configure_message_options(view);
    apply_message_theme(view, role);
    configure_message_auto_size(view);
    update_round_region(message);
    if (role == CHAT_ROLE_USER) SetWindowTextW(view, message->markdown);
    layout_history(true);
    return message;
}

static void reflow_all_messages(void) {
    size_t index;
    int width = history_bubble_width();
    bool keep_bottom = g_app.follow_bottom;
    for (index = 0; index < g_app.message_count; index++) {
        ChatMessage *message = &g_app.messages[index];
        if (message->width == width) continue;
        message->width = width;
        SetWindowPos(message->view, NULL, 0, 0, message->width,
            message->height, SWP_NOMOVE | SWP_NOACTIVATE |
            SWP_NOZORDER);
        update_round_region(message);
    }
    layout_history(keep_bottom);
}

static bool input_is_blank(const wchar_t *text) {
    if (!text) return true;
    while (*text) {
        if (!iswspace(*text)) return false;
        text++;
    }
    return true;
}

static wchar_t *compose_message(const wchar_t *prefix, const wchar_t *text) {
    size_t prefix_length = wcslen(prefix);
    size_t text_length = text ? wcslen(text) : 0;
    wchar_t *result = (wchar_t *)malloc(
        (prefix_length + text_length + 1) * sizeof(*result));
    if (!result) return NULL;
    memcpy(result, prefix, prefix_length * sizeof(*result));
    if (text_length)
        memcpy(result + prefix_length, text, text_length * sizeof(*result));
    result[prefix_length + text_length] = 0;
    return result;
}

static void finish_stream(void) {
    KillTimer(g_app.main_window, TIMER_STREAM);
    g_app.streaming = false;
    g_app.active_template = NULL;
    free(g_app.active_utf8);
    g_app.active_utf8 = NULL;
    g_app.active_utf8_length = 0;
    EnableWindow(g_app.send, TRUE);
}

static void stream_next_chunk(void) {
    ChatMessage *message;
    size_t end;
    TintaStreamChunk chunk;
    if (!g_app.streaming || !g_app.active_utf8 ||
        g_app.active_message >= g_app.message_count) {
        finish_stream();
        return;
    }
    message = &g_app.messages[g_app.active_message];
    if (g_app.stream_delta >= 128) {
        if (!SendMessageW(message->view, TMM_STREAM_END, 0, 0))
            SendMessageW(message->view, TMM_STREAM_CANCEL, 0, 0);
        finish_stream();
        return;
    }
    end = g_app.stream_delta == 127 ? g_app.active_utf8_length :
        (g_app.active_utf8_length * (g_app.stream_delta + 1)) / 128;
    while (end < g_app.active_utf8_length &&
           ((unsigned char)g_app.active_utf8[end] & 0xc0) == 0x80)
        end++;
    if (end <= g_app.stream_position) {
        end = g_app.stream_position + 1;
        while (end < g_app.active_utf8_length &&
               ((unsigned char)g_app.active_utf8[end] & 0xc0) == 0x80)
            end++;
    }
    memset(&chunk, 0, sizeof(chunk));
    chunk.cb_size = sizeof(chunk);
    chunk.utf8 = g_app.active_utf8 + g_app.stream_position;
    chunk.utf8_length = end - g_app.stream_position;
    if (!SendMessageW(message->view, TMM_STREAM_APPEND, 0, (LPARAM)&chunk)) {
        SendMessageW(message->view, TMM_STREAM_CANCEL, 0, 0);
        MessageBoxW(g_app.main_window, L"Unable to append the demo response.",
                    L"Tinta Core Chat Demo", MB_OK | MB_ICONERROR);
        finish_stream();
        return;
    }
    g_app.stream_position = end;
    g_app.stream_delta++;
}

static char *utf8_from_wide(const wchar_t *text, size_t *length) {
    int required;
    char *result;
    required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                   NULL, 0, NULL, NULL);
    if (required <= 1) return NULL;
    result = (char *)malloc((size_t)required);
    if (!result) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                             result, required, NULL, NULL)) {
        free(result);
        return NULL;
    }
    *length = (size_t)required - 1;
    return result;
}

static bool begin_assistant_response(void) {
    TintaStreamBegin begin;
    TintaStreamChunk prefix;
    ChatMessage *assistant = create_message(
        CHAT_ROLE_ASSISTANT, L"");
    if (!assistant) return false;
    g_app.active_message = g_app.message_count - 1;
    g_app.active_template = RESPONSE_TEMPLATES[g_app.next_template];
    g_app.next_template = (g_app.next_template + 1) %
        _countof(RESPONSE_TEMPLATES);
    g_app.active_utf8 = utf8_from_wide(
        g_app.active_template, &g_app.active_utf8_length);
    if (!g_app.active_utf8 || g_app.active_utf8_length < 128) return false;
    memset(&begin, 0, sizeof(begin));
    begin.cb_size = sizeof(begin);
    begin.base_uri = g_app.stream_base_uri;
    begin.format = TINTA_FORMAT_MARKDOWN;
    begin.refresh_interval_ms = 0;
    if (!SendMessageW(assistant->view, TMM_STREAM_BEGIN, 0, (LPARAM)&begin))
        return false;
    memset(&prefix, 0, sizeof(prefix));
    prefix.cb_size = sizeof(prefix);
    prefix.utf8 = ASSISTANT_STREAM_PREFIX;
    prefix.utf8_length = strlen(ASSISTANT_STREAM_PREFIX);
    if (!SendMessageW(assistant->view, TMM_STREAM_APPEND, 0,
                      (LPARAM)&prefix)) {
        SendMessageW(assistant->view, TMM_STREAM_CANCEL, 0, 0);
        return false;
    }
    g_app.stream_position = 0;
    g_app.stream_delta = 0;
    g_app.streaming = true;
    EnableWindow(g_app.send, FALSE);
    return SetTimer(g_app.main_window, TIMER_STREAM, 20, NULL) != 0;
}

static void send_input(void) {
    int length;
    wchar_t *input;
    wchar_t *markdown;
    if (g_app.streaming) {
        MessageBeep(MB_OK);
        return;
    }
    length = GetWindowTextLengthW(g_app.input);
    input = (wchar_t *)malloc(((size_t)length + 1) * sizeof(*input));
    if (!input) return;
    GetWindowTextW(g_app.input, input, length + 1);
    if (input_is_blank(input)) {
        free(input);
        MessageBeep(MB_OK);
        return;
    }
    markdown = compose_message(USER_PREFIX, input);
    free(input);
    if (!markdown) return;
    if (!create_message(CHAT_ROLE_USER, markdown)) {
        free(markdown);
        MessageBoxW(g_app.main_window, L"Unable to create the user message.",
                    L"Tinta Core Chat Demo", MB_OK | MB_ICONERROR);
        return;
    }
    free(markdown);
    SetWindowTextW(g_app.input, L"");
    g_app.follow_bottom = true;
    layout_history(true);
    if (!begin_assistant_response()) {
        finish_stream();
        MessageBoxW(g_app.main_window, L"Unable to start the demo response.",
                    L"Tinta Core Chat Demo", MB_OK | MB_ICONERROR);
    }
}

static LRESULT CALLBACK input_subclass(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam, UINT_PTR subclass_id, DWORD_PTR reference) {
    HWND owner = (HWND)reference;
    (void)subclass_id;
    if (message == WM_KEYDOWN && wparam == VK_RETURN &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        SendMessageW(owner, WM_COMMAND, MAKEWPARAM(ID_SEND, BN_CLICKED),
                     (LPARAM)g_app.send);
        return 0;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, input_subclass, 1);
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static LRESULT handle_history_notify(LPARAM lparam) {
    NMHDR *header = (NMHDR *)lparam;
    if (!header) return 0;
    if (header->code == TMN_AUTOSIZED) {
        const TintaAutoSizeNotify *notice =
            (const TintaAutoSizeNotify *)header;
        ChatMessage *message = find_message(header->hwndFrom);
        if (message) {
            message->height = notice->new_window_height;
            update_round_region(message);
            layout_history(false);
        }
        return TRUE;
    }
    if (header->code == TMN_DOCUMENTREADY ||
        header->code == TMN_STREAMUPDATED ||
        header->code == TMN_CONTENTUPDATED) return TRUE;
    if (header->code == TMN_LINKACTIVATE) {
        const TintaLinkNotify *link = (const TintaLinkNotify *)header;
        wchar_t text[1200];
        _snwprintf_s(text, _countof(text), _TRUNCATE,
            L"The chat host intercepted this link:\n\n%s", link->uri);
        MessageBoxW(g_app.main_window, text, L"Tinta Core Chat Demo", MB_OK);
        return TRUE;
    }
    if (header->code == TMN_RESOURCEOPENING) return TINTA_RESOURCE_DEFAULT;
    if (header->code == TMN_REQUESTFIND) {
        MessageBeep(MB_OK);
        return TRUE;
    }
    if (header->code == TMN_ERROR) {
        const TintaErrorNotify *error = (const TintaErrorNotify *)header;
        MessageBoxW(g_app.main_window, error->message, error->operation,
                    MB_OK | MB_ICONERROR);
        return TRUE;
    }
    return 0;
}

static void history_scroll_command(int command) {
    RECT client;
    SCROLLINFO info;
    int position = g_app.scroll_y;
    int line = scale_value(g_app.history, 56);
    GetClientRect(g_app.history, &client);
    switch (command) {
        case SB_LINEUP: position -= line; break;
        case SB_LINEDOWN: position += line; break;
        case SB_PAGEUP: position -= client.bottom - client.top; break;
        case SB_PAGEDOWN: position += client.bottom - client.top; break;
        case SB_TOP: position = 0; break;
        case SB_BOTTOM: position = history_max_scroll(); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            memset(&info, 0, sizeof(info));
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(g_app.history, SB_VERT, &info);
            position = info.nTrackPos;
            break;
        default: return;
    }
    set_history_scroll(position, true);
}

static LRESULT CALLBACK history_proc(HWND hwnd, UINT message,
                                     WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_SIZE:
            if (g_app.main_window)
                SetTimer(g_app.main_window, TIMER_REFLOW, 100, NULL);
            update_history_scrollbar();
            return 0;
        case WM_VSCROLL:
            history_scroll_command(LOWORD(wparam));
            return 0;
        case WM_MOUSEWHEEL: {
            int steps;
            g_app.wheel_remainder += GET_WHEEL_DELTA_WPARAM(wparam);
            steps = g_app.wheel_remainder / WHEEL_DELTA;
            g_app.wheel_remainder %= WHEEL_DELTA;
            if (steps)
                set_history_scroll(g_app.scroll_y -
                    steps * scale_value(hwnd, 72), true);
            return 0;
        }
        case WM_MOUSEHWHEEL:
            return 0;
        case WM_KEYDOWN:
            switch (wparam) {
                case VK_PRIOR: history_scroll_command(SB_PAGEUP); return 0;
                case VK_NEXT: history_scroll_command(SB_PAGEDOWN); return 0;
                case VK_HOME: history_scroll_command(SB_TOP); return 0;
                case VK_END: history_scroll_command(SB_BOTTOM); return 0;
            }
            break;
        case WM_NOTIFY:
            return handle_history_notify(lparam);
        case WM_PAINT: {
            PAINTSTRUCT paint;
            RECT client;
            HBRUSH background = (HBRUSH)GetClassLongPtrW(
                hwnd, GCLP_HBRBACKGROUND);
            BeginPaint(hwnd, &paint);
            GetClientRect(hwnd, &client);
            FillRect(paint.hdc, &client, background);
            if (!g_app.message_count) {
                HFONT old_font = (HFONT)SelectObject(paint.hdc, g_app.ui_font);
                SetBkMode(paint.hdc, TRANSPARENT);
                SetTextColor(paint.hdc, RGB(100, 116, 139));
                DrawTextW(paint.hdc,
                    L"Type a message below. Ctrl+Enter sends it.", -1,
                    &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(paint.hdc, old_font);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void layout_main_window(HWND hwnd) {
    RECT client;
    int margin;
    int gap;
    int composer_height;
    int button_width;
    int history_height;
    GetClientRect(hwnd, &client);
    margin = scale_value(hwnd, 14);
    gap = scale_value(hwnd, 10);
    composer_height = scale_value(hwnd, 108);
    button_width = scale_value(hwnd, 116);
    history_height = maximum_int(1, client.bottom - composer_height - margin * 2);
    if (g_app.history)
        MoveWindow(g_app.history, 0, 0, client.right, history_height, TRUE);
    if (g_app.input)
        MoveWindow(g_app.input, margin, history_height + margin,
            maximum_int(1, client.right - margin * 2 - button_width - gap),
            composer_height, TRUE);
    if (g_app.send)
        MoveWindow(g_app.send,
            maximum_int(margin, client.right - margin - button_width),
            history_height + margin, button_width, composer_height, TRUE);
}

static void create_ui_font(HWND hwnd) {
    int height = -MulDiv(10, (int)GetDpiForWindow(hwnd), 72);
    g_app.ui_font = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT message,
                                  WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            g_app.main_window = hwnd;
            create_ui_font(hwnd);
            g_app.history = CreateWindowExW(0, L"TintaChatHistory", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                0, 0, 1, 1, hwnd, (HMENU)ID_HISTORY, g_app.instance, NULL);
            g_app.input = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                0, 0, 1, 1, hwnd, (HMENU)ID_INPUT, g_app.instance, NULL);
            g_app.send = CreateWindowW(L"BUTTON", L"&Send\n(Ctrl+Enter)",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_MULTILINE,
                0, 0, 1, 1, hwnd, (HMENU)ID_SEND, g_app.instance, NULL);
            if (!g_app.history || !g_app.input || !g_app.send) return -1;
            SendMessageW(g_app.input, WM_SETFONT, (WPARAM)g_app.ui_font, TRUE);
            SendMessageW(g_app.send, WM_SETFONT, (WPARAM)g_app.ui_font, TRUE);
            SendMessageW(g_app.input, EM_SETLIMITTEXT, 65535, 0);
            SetWindowSubclass(g_app.input, input_subclass, 1,
                              (DWORD_PTR)hwnd);
            g_app.follow_bottom = true;
            g_app.next_control_id = ID_MESSAGE_FIRST;
            layout_main_window(hwnd);
            return 0;
        case WM_SIZE:
            layout_main_window(hwnd);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO *info = (MINMAXINFO *)lparam;
            info->ptMinTrackSize.x = scale_value(hwnd, 620);
            info->ptMinTrackSize.y = scale_value(hwnd, 520);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == ID_SEND &&
                (HIWORD(wparam) == BN_CLICKED || HIWORD(wparam) == 0)) {
                send_input();
                return 0;
            }
            break;
        case WM_TIMER:
            if (wparam == TIMER_STREAM) {
                stream_next_chunk();
                return 0;
            }
            if (wparam == TIMER_REFLOW) {
                KillTimer(hwnd, TIMER_REFLOW);
                reflow_all_messages();
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_STREAM);
            KillTimer(hwnd, TIMER_REFLOW);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void cleanup_app(void) {
    size_t index;
    for (index = 0; index < g_app.message_count; index++)
        free(g_app.messages[index].markdown);
    free(g_app.messages);
    free(g_app.active_utf8);
    if (g_app.local_image_path[0]) DeleteFileW(g_app.local_image_path);
    if (g_app.ui_font) DeleteObject(g_app.ui_font);
    memset(&g_app, 0, sizeof(g_app));
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show) {
    INITCOMMONCONTROLSEX common;
    WNDCLASSW main_class;
    WNDCLASSW history_class;
    HBRUSH main_background;
    HBRUSH history_background;
    HWND window;
    MSG message;
    int result;
    (void)previous;
    (void)command_line;
    SetProcessDPIAware();
    memset(&g_app, 0, sizeof(g_app));
    g_app.instance = instance;
    if (!prepare_local_image()) return 1;
    common.dwSize = sizeof(common);
    common.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&common);
    if (FAILED(TintaCoreInitialize())) return 1;
    main_background = CreateSolidBrush(RGB(241, 245, 249));
    history_background = CreateSolidBrush(RGB(248, 250, 252));
    memset(&history_class, 0, sizeof(history_class));
    history_class.lpfnWndProc = history_proc;
    history_class.hInstance = instance;
    history_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    history_class.hbrBackground = history_background;
    history_class.lpszClassName = L"TintaChatHistory";
    memset(&main_class, 0, sizeof(main_class));
    main_class.lpfnWndProc = main_proc;
    main_class.hInstance = instance;
    main_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    main_class.hbrBackground = main_background;
    main_class.lpszClassName = L"TintaChatDemoHost";
    if (!RegisterClassW(&history_class) || !RegisterClassW(&main_class)) {
        DeleteObject(history_background);
        DeleteObject(main_background);
        TintaCoreUninitialize();
        return 1;
    }
    window = CreateWindowExW(0, main_class.lpszClassName,
        L"Tinta Core Chat Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 760,
        NULL, NULL, instance, NULL);
    if (!window) {
        UnregisterClassW(main_class.lpszClassName, instance);
        UnregisterClassW(history_class.lpszClassName, instance);
        DeleteObject(history_background);
        DeleteObject(main_background);
        TintaCoreUninitialize();
        return 1;
    }
    ShowWindow(window, show);
    UpdateWindow(window);
    SetFocus(g_app.input);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    result = (int)message.wParam;
    cleanup_app();
    UnregisterClassW(main_class.lpszClassName, instance);
    UnregisterClassW(history_class.lpszClassName, instance);
    DeleteObject(history_background);
    DeleteObject(main_background);
    TintaCoreUninitialize();
    return result;
}
