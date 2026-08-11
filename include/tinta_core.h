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

#define TINTA_CORE_VERSION_MAJOR 1
#define TINTA_CORE_VERSION_MINOR 2
#define TINTA_CORE_VERSION_PATCH 0

/* Register this class with TintaCoreInitialize before calling CreateWindowExW. */
#define TINTA_MARKDOWN_VIEW_CLASSW L"Tinta.MarkdownView"

/*
 * Public ABI conventions
 * ----------------------
 * - Set cb_size to sizeof(the structure) before sending it to the control.
 * - Input strings need not be NUL terminated when an explicit length is given.
 * - Input data is copied before SendMessage returns unless stated otherwise.
 * - Pointers received through WM_NOTIFY are valid only during that notification.
 * - Create, message, and destroy each HWND on its owning UI thread.
 */

typedef enum TintaDocumentFormat {
    TINTA_FORMAT_AUTO = 0,
    TINTA_FORMAT_MARKDOWN = 1,
    TINTA_FORMAT_MERMAID = 2
} TintaDocumentFormat;

typedef struct TintaDocument {
    UINT cb_size;
    const char *utf8;             /* UTF-8 source. */
    size_t utf8_length;           /* Bytes, excluding any trailing NUL. */
    const wchar_t *base_uri;      /* Optional file path or HTTP(S) base URI. */
    TintaDocumentFormat format;
    DWORD flags;                  /* Reserved; set to zero. */
} TintaDocument;

/* Starts a new empty streamed document and replaces any current document. */
typedef struct TintaStreamBegin {
    UINT cb_size;
    const wchar_t *base_uri;      /* Fixed for the lifetime of this stream. */
    TintaDocumentFormat format;
    UINT refresh_interval_ms;     /* 0 = 50 ms; otherwise 20 through 1000 ms. */
    DWORD flags;                  /* Reserved; set to zero. */
} TintaStreamBegin;

/*
 * A stream chunk may split a UTF-8 character. Invalid chunks are rejected
 * atomically; incomplete trailing characters remain pending for the next chunk.
 */
typedef struct TintaStreamChunk {
    UINT cb_size;
    const char *utf8;
    size_t utf8_length;           /* Bytes; arbitrary SSE/delta boundaries. */
    DWORD flags;                  /* Reserved; set to zero. */
} TintaStreamChunk;

/* TintaOptions::flags. */
enum {
    TINTA_OPTION_SELECTION = 0x0001,
    TINTA_OPTION_KEYBOARD_NAVIGATION = 0x0002,
    TINTA_OPTION_MOUSE_ZOOM = 0x0004,
    TINTA_OPTION_LOCAL_IMAGES = 0x0010,
    TINTA_OPTION_REMOTE_IMAGES = 0x0020,
    TINTA_OPTION_OPEN_UNHANDLED_LINKS = 0x0040,
    TINTA_OPTION_DOCUMENT_COPY_BUTTON = 0x0080
};

typedef struct TintaOptions {
    UINT cb_size;
    DWORD flags;
} TintaOptions;

/* TintaAutoSize::flags. Heights are specified in 96-DPI device-independent pixels. */
enum {
    TINTA_AUTOSIZE_HEIGHT = 0x0001,
    TINTA_AUTOSIZE_MAX_HEIGHT = 0x0002
};

/*
 * Optional height management. The host continues to own the control width and
 * position. When the maximum is reached, the control uses its internal scroll bar.
 */
typedef struct TintaAutoSize {
    UINT cb_size;
    DWORD flags;
    float min_height;             /* 0 = no explicit minimum. */
    float max_height;             /* Used with TINTA_AUTOSIZE_MAX_HEIGHT. */
} TintaAutoSize;

/* Page margins in 96-DPI device-independent pixels. */
typedef struct TintaPageMargins {
    UINT cb_size;
    float left;
    float top;
    float right;
    float bottom;
} TintaPageMargins;

typedef struct TintaLimits {
    UINT cb_size;
    size_t max_document_bytes;    /* UTF-8 bytes, including buffered stream data. */
    size_t max_ast_nodes;
    size_t max_ast_depth;
    size_t max_mermaid_nodes;
    size_t max_mermaid_edges;
    uint64_t max_image_pixels;    /* Per decoded image: width * height. */
    uint64_t max_remote_image_bytes;
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
    /* Colors use 0xRRGGBB. */
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
    size_t index;                 /* Input: zero-based heading index. */
    int level;
    wchar_t *text;                /* Optional caller-owned output buffer. */
    size_t text_capacity;
    wchar_t *anchor;              /* Optional caller-owned output buffer. */
    size_t anchor_capacity;
} TintaHeadingInfo;

typedef struct TintaSelection {
    UINT cb_size;
    size_t start;                 /* UTF-16 offset in rendered document text. */
    size_t end;                   /* Exclusive UTF-16 offset. */
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
    TINTA_RESOURCE_REPLACE = 2    /* Set replacement_uri before returning. */
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
    const wchar_t *replacement_uri; /* Host output for TINTA_RESOURCE_REPLACE. */
} TintaResourceNotify;

typedef struct TintaResourceErrorNotify {
    NMHDR hdr;
    TintaResourceKind kind;
    const wchar_t *resolved_uri;
    HRESULT error;
} TintaResourceErrorNotify;

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
    uint64_t revision;            /* Monotonic within the current stream. */
    size_t utf8_length;           /* Bytes committed to this displayed revision. */
    TintaContentSize content_size;
} TintaStreamUpdateNotify;

enum {
    TINTA_CONTENT_UPDATE_RESOURCE = 0x0001
};

typedef struct TintaContentUpdateNotify {
    NMHDR hdr;
    DWORD flags;
    uint64_t revision;            /* Text revision whose layout changed. */
    size_t utf8_length;
    TintaContentSize content_size;
} TintaContentUpdateNotify;

typedef struct TintaAutoSizeNotify {
    NMHDR hdr;
    int old_client_height;
    int new_client_height;
    int new_window_height;
    TintaContentSize content_size;
} TintaAutoSizeNotify;

enum {
    TINTA_CAPABILITY_UIA = 0x0001,
    TINTA_CAPABILITY_MERMAID = 0x0002,
    TINTA_CAPABILITY_SYNTAX = 0x0004,
    TINTA_CAPABILITY_REMOTE_IMAGES = 0x0008,
    TINTA_CAPABILITY_LOCAL_IMAGES = 0x0010,
    TINTA_CAPABILITY_STREAMING = 0x0020,
    TINTA_CAPABILITY_SVG = 0x0040,
    TINTA_CAPABILITY_MATH = 0x0080
};

typedef struct TintaVersionInfo {
    UINT cb_size;
    UINT major;
    UINT minor;
    UINT patch;
} TintaVersionInfo;

typedef struct TintaCapabilities {
    UINT cb_size;
    DWORD flags;
    DWORD option_flags;           /* TintaOptions flags supported by this build. */
} TintaCapabilities;

typedef struct TintaStats {
    UINT cb_size;
    uint64_t document_revision;
    size_t source_bytes;
    size_t ast_nodes;
    size_t text_runs;
    size_t image_resources;
    size_t parse_time_us;
    size_t layout_time_us;
    size_t draw_calls;
} TintaStats;

/*
 * Control messages. Unless described otherwise, wParam is zero and a mutating
 * message returns nonzero on success.
 *
 * TMM_SETDOCUMENT       lParam = const TintaDocument *; replaces active stream.
 * TMM_SETBASEURI        lParam = const wchar_t *; rejected during a stream.
 * TMM_SET/GETOPTIONS    lParam = TintaOptions *.
 * TMM_SET/GETAUTOSIZE   lParam = TintaAutoSize *.
 * TMM_SET/GETPAGEMARGINS lParam = TintaPageMargins *.
 * TMM_SET/GETLIMITS     lParam = TintaLimits *.
 * TMM_SETBUILTINTHEME   wParam = TintaBuiltinTheme.
 * TMM_SETCUSTOMTHEME    lParam = const TintaThemeSpec *.
 * TMM_SET/GETZOOM       lParam points to a float.
 * TMM_SET/GETSCROLLPOS  lParam = TintaScrollPosition *.
 * TMM_GETCONTENTSIZE    lParam = TintaContentSize *.
 * TMM_FIND              lParam = const TintaFindRequest *.
 * TMM_GETFINDSTATE      lParam = TintaFindState *.
 * TMM_GETHEADINGCOUNT   returns the number of headings.
 * TMM_GETHEADING        lParam = TintaHeadingInfo *.
 * TMM_SCROLLTOHEADING   wParam = zero-based heading index.
 * TMM_GETSELECTION      lParam = TintaSelection *.
 * TMM_GETVERSION        lParam = TintaVersionInfo *.
 * TMM_GETCAPABILITIES   lParam = TintaCapabilities *.
 * TMM_GETSTATS          lParam = TintaStats *.
 */
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

/*
 * Streaming state machine
 * -----------------------
 * BEGIN  lParam = const TintaStreamBegin *; starts a new empty document.
 * APPEND lParam = const TintaStreamChunk *; copies and queues one UTF-8 delta.
 * END    validates the final UTF-8 character and forces the final revision.
 * CANCEL discards undisplayed data and keeps the last displayed revision.
 *
 * A new BEGIN, WM_SETTEXT, or TMM_SETDOCUMENT replaces an active stream.
 * Intermediate parses/layouts are coalesced to the configured refresh rate.
 */
#define TMM_STREAM_BEGIN          (TMM_FIRST + 25)
#define TMM_STREAM_APPEND         (TMM_FIRST + 26)
#define TMM_STREAM_END            (TMM_FIRST + 27)
#define TMM_STREAM_CANCEL         (TMM_FIRST + 28)
#define TMM_SETAUTOSIZE           (TMM_FIRST + 29)
#define TMM_GETAUTOSIZE           (TMM_FIRST + 30)
#define TMM_GETVERSION            (TMM_FIRST + 31)
#define TMM_GETCAPABILITIES       (TMM_FIRST + 32)
#define TMM_GETSTATS              (TMM_FIRST + 33)
#define TMM_SETPAGEMARGINS        (TMM_FIRST + 34)
#define TMM_GETPAGEMARGINS        (TMM_FIRST + 35)

/*
 * WM_NOTIFY codes sent synchronously to the parent window.
 *
 * TMN_DOCUMENTREADY   Final text layout is ready; images may still be loading.
 * TMN_ERROR           lParam = const TintaErrorNotify *.
 * TMN_LINKACTIVATE    lParam = const TintaLinkNotify *; nonzero means handled.
 * TMN_RESOURCEOPENING lParam = TintaResourceNotify *; return TintaResourceAction.
 * TMN_RESOURCEERROR   lParam = const TintaResourceErrorNotify *.
 * TMN_CONTEXTMENU     lParam = const TintaContextMenuNotify *.
 * TMN_STREAMUPDATED   lParam = const TintaStreamUpdateNotify *.
 * TMN_CONTENTUPDATED  lParam = const TintaContentUpdateNotify *; for example,
 *                     an image completion that changes content size.
 * TMN_AUTOSIZED        lParam = const TintaAutoSizeNotify *; the parent should
 *                     reflow sibling controls and update outer scroll ranges.
 */
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
#define TMN_AUTOSIZED             (TMN_FIRST - 14)

/*
 * Initialization is process-wide and reference counted. Pair every successful
 * (SUCCEEDED) call with TintaCoreUninitialize after all owned controls are gone.
 */
TINTA_CORE_API HRESULT TintaCoreInitialize(void);
TINTA_CORE_API void TintaCoreUninitialize(void);

#ifdef __cplusplus
}
#endif

#endif
