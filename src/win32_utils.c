#include "app.h"

#include <stdlib.h>

COLORREF tinta_color(uint32_t rgb) {
    return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

bool tinta_utf8_to_utf16(const char *text, size_t length, TintaStr16 *output) {
    const char *source = text ? text : "";
    int count;
    if (!text && length) return false;
    if (length > INT_MAX) return false;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, (int)length, NULL, 0);
    if (!count && length) return false;
    if (!tinta_str16_reserve(output, (size_t)count)) return false;
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, (int)length, output->data, count);
    output->len = (size_t)count;
    output->data[count] = 0;
    return true;
}

bool tinta_utf16_to_utf8(const wchar_t *text, size_t length, TintaStr8 *output) {
    const wchar_t *source = text ? text : L"";
    int count;
    if (!text && length) return false;
    if (length > INT_MAX) return false;
    count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, (int)length, NULL, 0, NULL, NULL);
    if (!count && length) return false;
    if (!tinta_str8_reserve(output, (size_t)count)) return false;
    if (count) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, (int)length, output->data, count, NULL, NULL);
    output->len = (size_t)count;
    output->data[count] = 0;
    return true;
}

wchar_t *tinta_wcsdup_n(const wchar_t *text, size_t length) {
    wchar_t *copy = (wchar_t *)malloc((length + 1) * sizeof(*copy));
    if (!copy) return NULL;
    if (length) memcpy(copy, text, length * sizeof(*copy));
    copy[length] = 0;
    return copy;
}

bool tinta_read_file_bytes(const wchar_t *path, TintaStr8 *output) {
    HANDLE file;
    LARGE_INTEGER size;
    TintaStr8 loaded = {0};
    size_t offset = 0;
    bool success = false;
    if (!path || !output) return false;
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (uint64_t)size.QuadPart > SIZE_MAX - 1 ||
        !tinta_str8_reserve(&loaded, (size_t)size.QuadPart))
        goto done;
    while (offset < (size_t)size.QuadPart) {
        size_t remaining = (size_t)size.QuadPart - offset;
        DWORD chunk = (DWORD)(remaining > MAXDWORD ? MAXDWORD : remaining);
        DWORD read = 0;
        if (!ReadFile(file, loaded.data + offset, chunk, &read, NULL) ||
            read != chunk)
            goto done;
        offset += read;
    }
    loaded.len = offset;
    loaded.data[offset] = 0;
    tinta_str8_destroy(output);
    *output = loaded;
    memset(&loaded, 0, sizeof(loaded));
    success = true;
done:
    tinta_str8_destroy(&loaded);
    CloseHandle(file);
    return success;
}

bool tinta_set_clipboard_text(HWND owner, const wchar_t *text, size_t length) {
    HGLOBAL memory;
    wchar_t *copy;
    bool opened = false;
    bool success = false;
    int attempt;
    if ((!text && length) || length > (SIZE_MAX / sizeof(wchar_t)) - 1)
        return false;
    memory = GlobalAlloc(GMEM_MOVEABLE, (length + 1) * sizeof(wchar_t));
    if (!memory) return false;
    copy = (wchar_t *)GlobalLock(memory);
    if (!copy) {
        GlobalFree(memory);
        return false;
    }
    if (length) memcpy(copy, text, length * sizeof(wchar_t));
    copy[length] = 0;
    GlobalUnlock(memory);
    for (attempt = 0; attempt < 6 && !opened; attempt++) {
        opened = OpenClipboard(owner) || OpenClipboard(NULL);
        if (!opened) Sleep(2);
    }
    if (opened) {
        BOOL emptied = EmptyClipboard();
        HANDLE stored = emptied ? SetClipboardData(CF_UNICODETEXT, memory) : NULL;
        if (emptied && stored) {
            success = true;
            memory = NULL;
        }
        CloseClipboard();
    }
    if (memory) GlobalFree(memory);
    return success;
}

bool tinta_get_clipboard_text(HWND owner, TintaStr16 *output) {
    HANDLE data;
    const wchar_t *text;
    bool opened = false;
    bool success = false;
    int attempt;
    if (!output) return false;
    tinta_str16_clear(output);
    for (attempt = 0; attempt < 6 && !opened; attempt++) {
        opened = OpenClipboard(owner) || OpenClipboard(NULL);
        if (!opened) Sleep(2);
    }
    if (!opened) return false;
    data = GetClipboardData(CF_UNICODETEXT);
    text = data ? (const wchar_t *)GlobalLock(data) : NULL;
    if (text) {
        success = tinta_str16_assign(output, text, wcslen(text));
        GlobalUnlock(data);
    }
    CloseClipboard();
    return success;
}
