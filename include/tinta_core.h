#ifndef TINTA_CORE_PUBLIC_H
#define TINTA_CORE_PUBLIC_H

#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TINTA_CORE_SHARED)
#  if defined(TINTA_CORE_BUILD)
#    define TINTA_CORE_API __declspec(dllexport)
#  else
#    define TINTA_CORE_API __declspec(dllimport)
#  endif
#else
#  define TINTA_CORE_API
#endif

#define TINTA_CORE_VERSION_MAJOR 0
#define TINTA_CORE_VERSION_MINOR 2
#define TINTA_CORE_VERSION_PATCH 0

#define TINTA_MARKDOWN_VIEW_CLASSW L"Tinta.MarkdownView"

typedef enum TintaDocumentFormat {
    TINTA_FORMAT_AUTO = 0,
    TINTA_FORMAT_MARKDOWN = 1,
    TINTA_FORMAT_MERMAID = 2
} TintaDocumentFormat;

typedef struct TintaDocument {
    UINT cb_size;
    const char *utf8;
    size_t utf8_length;
    const wchar_t *base_uri;
    TintaDocumentFormat format;
    DWORD flags;
} TintaDocument;

typedef struct TintaStreamBegin {
    UINT cb_size;
    const wchar_t *base_uri;
    TintaDocumentFormat format;
    UINT refresh_interval_ms;
    DWORD flags;
} TintaStreamBegin;

typedef struct TintaStreamChunk {
    UINT cb_size;
    const char *utf8;
    size_t utf8_length;
    DWORD flags;
} TintaStreamChunk;

enum {
    TINTA_OPTION_SELECTION = 0x0001,
    TINTA_OPTION_KEYBOARD_NAVIGATION = 0x0002,
    TINTA_OPTION_MOUSE_ZOOM = 0x0004,
    TINTA_OPTION_CODE_COPY_BUTTON = 0x0008,
    TINTA_OPTION_LOCAL_IMAGES = 0x0010,
    TINTA_OPTION_REMOTE_IMAGES = 0x0020,
    TINTA_OPTION_OPEN_UNHANDLED_LINKS = 0x0040
};

typedef struct TintaOptions {
    UINT cb_size;
    DWORD flags;
} TintaOptions;

typedef struct TintaLimits {
    UINT cb_size;
    size_t max_document_bytes;
    size_t max_ast_nodes;
    uint64_t max_image_pixels;
    UINT max_image_resources;
    UINT max_concurrent_downloads;
} TintaLimits;

typedef enum TintaBuiltinTheme {
    TINTA_THEME_SYSTEM = -1,
    TINTA_THEME_PAPER = 0,
    TINTA_THEME_SAKURA,
    TINTA_THEME_ARCTIC,
    TINTA_THEME_MEADOW,
    TINTA_THEME_DUSK,
    TINTA_THEME_MIDNIGHT,
    TINTA_THEME_DRACULA,
    TINTA_THEME_FOREST,
    TINTA_THEME_EMBER,
    TINTA_THEME_ABYSS
} TintaBuiltinTheme;

typedef struct TintaThemeSpec {
    UINT cb_size;
    const wchar_t *font_family;
    const wchar_t *code_font_family;
    BOOL dark;
    uint32_t background;
    uint32_t text;
    uint32_t heading;
    uint32_t link;
    uint32_t code;
    uint32_t code_background;
    uint32_t quote;
    uint32_t accent;
    uint32_t syntax_keyword;
    uint32_t syntax_string;
    uint32_t syntax_comment;
    uint32_t syntax_number;
    uint32_t syntax_function;
    uint32_t syntax_type;
    uint32_t syntax_control;
} TintaThemeSpec;

enum {
    TINTA_FIND_MATCH_CASE = 0x0001,
    TINTA_FIND_WHOLE_WORD = 0x0002,
    TINTA_FIND_WRAP = 0x0004
};

typedef struct TintaFindRequest {
    UINT cb_size;
    const wchar_t *text;
    size_t text_length;
    DWORD flags;
} TintaFindRequest;

typedef struct TintaFindState {
    UINT cb_size;
    size_t match_count;
    size_t current_index;
} TintaFindState;

typedef struct TintaHeadingInfo {
    UINT cb_size;
    size_t index;
    int level;
    wchar_t *text;
    size_t text_capacity;
    wchar_t *anchor;
    size_t anchor_capacity;
} TintaHeadingInfo;

typedef struct TintaSelection {
    UINT cb_size;
    size_t start;
    size_t end;
} TintaSelection;

typedef struct TintaScrollPosition {
    UINT cb_size;
    float x;
    float y;
} TintaScrollPosition;

typedef struct TintaContentSize {
    UINT cb_size;
    float width;
    float height;
} TintaContentSize;

typedef enum TintaResourceKind {
    TINTA_RESOURCE_LOCAL_IMAGE = 1,
    TINTA_RESOURCE_REMOTE_IMAGE = 2
} TintaResourceKind;

typedef enum TintaResourceAction {
    TINTA_RESOURCE_DEFAULT = 0,
    TINTA_RESOURCE_BLOCK = 1,
    TINTA_RESOURCE_REPLACE = 2
} TintaResourceAction;

typedef struct TintaLinkNotify {
    NMHDR hdr;
    const wchar_t *uri;
} TintaLinkNotify;

typedef struct TintaResourceNotify {
    NMHDR hdr;
    TintaResourceKind kind;
    const wchar_t *original_uri;
    const wchar_t *resolved_uri;
    const wchar_t *replacement_uri;
} TintaResourceNotify;

typedef struct TintaErrorNotify {
    NMHDR hdr;
    HRESULT error;
    const wchar_t *operation;
    const wchar_t *message;
} TintaErrorNotify;

typedef struct TintaContextMenuNotify {
    NMHDR hdr;
    POINT screen;
    BOOL has_selection;
    const wchar_t *link_uri;
    BOOL over_code_block;
} TintaContextMenuNotify;

typedef struct TintaStreamUpdateNotify {
    NMHDR hdr;
    uint64_t revision;
    size_t utf8_length;
    TintaContentSize content_size;
} TintaStreamUpdateNotify;

enum {
    TINTA_CONTENT_UPDATE_RESOURCE = 0x0001
};

typedef struct TintaContentUpdateNotify {
    NMHDR hdr;
    DWORD flags;
    uint64_t revision;
    size_t utf8_length;
    TintaContentSize content_size;
} TintaContentUpdateNotify;

#define TMM_FIRST                 (WM_USER + 0x500)
#define TMM_SETDOCUMENT           (TMM_FIRST + 0)
#define TMM_SETBASEURI            (TMM_FIRST + 1)
#define TMM_SETOPTIONS            (TMM_FIRST + 2)
#define TMM_GETOPTIONS            (TMM_FIRST + 3)
#define TMM_SETLIMITS             (TMM_FIRST + 4)
#define TMM_GETLIMITS             (TMM_FIRST + 5)
#define TMM_SETBUILTINTHEME       (TMM_FIRST + 6)
#define TMM_SETCUSTOMTHEME        (TMM_FIRST + 7)
#define TMM_SETZOOM               (TMM_FIRST + 8)
#define TMM_GETZOOM               (TMM_FIRST + 9)
#define TMM_SETSCROLLPOS          (TMM_FIRST + 10)
#define TMM_GETSCROLLPOS          (TMM_FIRST + 11)
#define TMM_GETCONTENTSIZE        (TMM_FIRST + 12)
#define TMM_FIND                  (TMM_FIRST + 13)
#define TMM_FINDNEXT              (TMM_FIRST + 14)
#define TMM_FINDPREVIOUS          (TMM_FIRST + 15)
#define TMM_CLEARFIND             (TMM_FIRST + 16)
#define TMM_GETFINDSTATE          (TMM_FIRST + 17)
#define TMM_GETHEADINGCOUNT       (TMM_FIRST + 18)
#define TMM_GETHEADING            (TMM_FIRST + 19)
#define TMM_SCROLLTOHEADING       (TMM_FIRST + 20)
#define TMM_SELECTALL             (TMM_FIRST + 21)
#define TMM_CLEARSELECTION        (TMM_FIRST + 22)
#define TMM_GETSELECTION          (TMM_FIRST + 23)
#define TMM_REFRESHAPPEARANCE     (TMM_FIRST + 24)
#define TMM_STREAM_BEGIN          (TMM_FIRST + 25)
#define TMM_STREAM_APPEND         (TMM_FIRST + 26)
#define TMM_STREAM_END            (TMM_FIRST + 27)
#define TMM_STREAM_CANCEL         (TMM_FIRST + 28)

#define TMN_FIRST                 ((UINT)-1800)
#define TMN_DOCUMENTREADY         (TMN_FIRST - 1)
#define TMN_ERROR                 (TMN_FIRST - 2)
#define TMN_LINKACTIVATE          (TMN_FIRST - 3)
#define TMN_RESOURCEOPENING       (TMN_FIRST - 4)
#define TMN_RESOURCEERROR         (TMN_FIRST - 5)
#define TMN_SCROLLCHANGED         (TMN_FIRST - 6)
#define TMN_ZOOMCHANGED           (TMN_FIRST - 7)
#define TMN_SELECTIONCHANGED      (TMN_FIRST - 8)
#define TMN_REQUESTFIND           (TMN_FIRST - 9)
#define TMN_CONTEXTMENU           (TMN_FIRST - 10)
#define TMN_COPYCOMPLETED         (TMN_FIRST - 11)
#define TMN_STREAMUPDATED         (TMN_FIRST - 12)
#define TMN_CONTENTUPDATED        (TMN_FIRST - 13)

TINTA_CORE_API HRESULT TintaCoreInitialize(void);
TINTA_CORE_API void TintaCoreUninitialize(void);

#ifdef __cplusplus
}
#endif

#endif
