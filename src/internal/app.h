#ifndef TINTA_APP_H
#define TINTA_APP_H

#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>

#include "document.h"
#include "features.h"
#if TINTA_ENABLE_MERMAID
#include "mermaid.h"
#endif
#include "win_graphics_c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TINTA_TIMER_NOTIFICATION 3
#define TINTA_TIMER_STREAM 6
#define TINTA_TIMER_BLOCK_ANIMATION 7
#define TINTA_WM_LAYOUT_CHUNK (WM_APP + 1)
#define TINTA_WM_IMAGE_READY (WM_APP + 2)
#define TINTA_WM_UIA_INVOKE (WM_APP + 3)
#define TINTA_WM_STREAM_PARSED (WM_APP + 4)

typedef struct TintaTheme {
    const wchar_t *name;
    const wchar_t *font_family;
    const wchar_t *code_font_family;
    bool dark;
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
} TintaTheme;

extern const TintaTheme TINTA_THEMES[10];
extern const size_t TINTA_THEME_COUNT;

typedef struct TintaSettings {
    int theme_index;
    float zoom;
    int width;
    int height;
} TintaSettings;

typedef struct TintaTextRun {
    wchar_t *text;
    size_t text_length;
    IDWriteTextLayout *layout;
    float x;
    float y;
    float width;
    float height;
    uint32_t color;
    float opacity;
    size_t doc_start;
    size_t doc_length;
    char *url;
    bool underline;
    bool strikethrough;
    size_t horizontal_region;
} TintaTextRun;

typedef enum TintaDrawShape {
    TINTA_DRAW_SHAPE_ROUNDED,
    TINTA_DRAW_SHAPE_ELLIPSE,
    TINTA_DRAW_SHAPE_DIAMOND,
    TINTA_DRAW_SHAPE_HEXAGON,
    TINTA_DRAW_SHAPE_STADIUM,
    TINTA_DRAW_SHAPE_RECTANGLE
} TintaDrawShape;

typedef struct TintaDrawRect {
    RECT rect;
    uint32_t color;
    float opacity;
    float radius;
    bool outline;
    float stroke;
    TintaDrawShape shape;
    size_t horizontal_region;
} TintaDrawRect;

typedef struct TintaDrawLine {
    POINT a;
    POINT b;
    uint32_t color;
    float stroke;
    float opacity;
    bool dashed;
    size_t horizontal_region;
} TintaDrawLine;

typedef struct TintaDrawBitmap {
    size_t resource_index;
    RECT rect;
} TintaDrawBitmap;

typedef struct TintaCodeBlock {
    RECT rect;
    wchar_t *text;
    wchar_t *language;
    size_t horizontal_region;
    size_t collapse_state;
    float expansion;
} TintaCodeBlock;

typedef struct TintaMermaidBlock {
    RECT rect;
    wchar_t *text;
    size_t horizontal_region;
    size_t collapse_state;
    float expansion;
} TintaMermaidBlock;

typedef enum TintaHorizontalRegionKind {
    TINTA_HORIZONTAL_CODE,
    TINTA_HORIZONTAL_MERMAID
} TintaHorizontalRegionKind;

typedef struct TintaHorizontalRegion {
    TintaHorizontalRegionKind kind;
    size_t source_offset;
    size_t ordinal;
    RECT viewport;
    float content_left;
    float content_right;
    float scroll_x;
    float scale_factor;
    size_t collapse_state;
    float expansion;
    bool overflow;
} TintaHorizontalRegion;

typedef struct TintaHorizontalScrollState {
    TintaHorizontalRegionKind kind;
    size_t source_offset;
    size_t ordinal;
    float scroll_dip;
} TintaHorizontalScrollState;

typedef struct TintaBlockCollapseState {
    TintaHorizontalRegionKind kind;
    size_t source_offset;
    size_t ordinal;
    float progress;
    float target;
    bool animating;
} TintaBlockCollapseState;

typedef struct TintaHeading {
    wchar_t *text;
    wchar_t *slug;
    int level;
    float y;
    size_t doc_start;
    size_t doc_length;
} TintaHeading;

typedef struct TintaImageResource {
    char *key;
    wchar_t *resolved_uri;
    IWICBitmapSource *source;
    ID2D1Bitmap *bitmap;
    UINT width;
    UINT height;
    bool remote;
    int state;
} TintaImageResource;

typedef enum TintaNoticeKind {
    TINTA_NOTICE_NONE,
    TINTA_NOTICE_COPIED
} TintaNoticeKind;

typedef struct TintaScrollbarGeometry {
    float left;
    float top;
    float right;
    float bottom;
    float track_length;
    float maximum_scroll;
} TintaScrollbarGeometry;

typedef struct TintaSearchMatch {
    size_t start;
    size_t length;
} TintaSearchMatch;

typedef struct TintaHitEntry {
    int bucket;
    size_t run_index;
} TintaHitEntry;

typedef struct TintaScrollAnchor {
    size_t source_offset;
    float rendered_y;
} TintaScrollAnchor;

#if TINTA_ENABLE_MERMAID
typedef struct TintaMermaidCacheEntry {
    const TintaElement *element;
    TintaMermaidParseResult parsed;
} TintaMermaidCacheEntry;
#endif

typedef struct TintaApp TintaApp;
typedef struct TintaImageAsync TintaImageAsync;
typedef struct TintaPreparedSource {
    TintaParseResult document;
    TintaStr8 source;
    size_t ast_nodes;
    bool focus_mermaid;
} TintaPreparedSource;
typedef bool (*TintaResolveImageFn)(TintaApp *app, const char *source,
                                    TintaStr16 *resolved, bool *remote,
                                    bool *blocked);
typedef void (*TintaInvokeLinkFn)(TintaApp *app, const char *url);
typedef void (*TintaResourceErrorFn)(TintaApp *app, bool remote,
                                     const wchar_t *resolved_uri,
                                     HRESULT error);

struct TintaApp {
    HINSTANCE instance;
    HWND hwnd;
    int width;
    int height;
    float dpi_scale;
    float zoom;
    bool com_initialized;
    float scroll_y;
    float scroll_x;
    float content_height;
    float content_width;
    float page_margin_left;
    float page_margin_top;
    float page_margin_right;
    float page_margin_bottom;
    int theme_index;
    const TintaTheme *theme;

    ID2D1Factory *d2d_factory;
    ID2D1HwndRenderTarget *render_target;
    ID2D1HwndRenderTarget *device_context;
    ID2D1SolidColorBrush *brush;
    IDWriteFactory *dwrite_factory;
    IDWriteFontFallback *font_fallback;
    IWICImagingFactory *wic_factory;
    IDWriteTextFormat *body_format;
    IDWriteTextFormat *bold_format;
    IDWriteTextFormat *italic_format;
    IDWriteTextFormat *bold_italic_format;
    IDWriteTextFormat *small_format;
    IDWriteTextFormat *code_format;
    IDWriteTextFormat *code_bold_format;
    IDWriteTextFormat *code_italic_format;
    IDWriteTextFormat *code_bold_italic_format;
    IDWriteTextFormat *heading_formats[6];
    IDWriteTextFormat *ui_format;
    IDWriteTextFormat *chrome_format;

    TintaParseResult document;
    uint64_t document_revision;
    TintaStr8 source;
    bool layout_dirty;
    bool layout_complete;
    bool layout_chunk_posted;
    bool focus_mermaid_on_next_layout;
    size_t layout_next_block;
    float layout_cursor_y;
    TintaVec text_runs;
    TintaVec rects;
    TintaVec lines;
    TintaVec bitmaps;
    TintaVec code_blocks;
    TintaVec mermaid_blocks;
    TintaVec horizontal_regions;
    TintaVec horizontal_scroll_states;
    TintaVec block_collapse_states;
    size_t active_horizontal_region;
    ULONGLONG block_animation_tick;
    TintaVec headings;
    TintaVec scroll_anchors;
    bool scroll_anchor_pending;
    size_t pending_scroll_source_offset;
    float pending_scroll_delta;
    TintaVec hit_entries;
    bool hit_index_dirty;
    TintaStr16 doc_text;
    TintaVec viewer_search_matches;
    int viewer_search_index;
    TintaStr16 search_query;
    TintaVec image_resources;
#if TINTA_ENABLE_MERMAID
    TintaVec mermaid_cache;
#endif
    TintaImageAsync *image_async;
    TintaResolveImageFn resolve_image;
    TintaInvokeLinkFn invoke_link;
    TintaResourceErrorFn resource_error;
    void *resource_context;
    size_t max_ast_nodes;
    size_t max_ast_depth;
    size_t max_mermaid_nodes;
    size_t max_mermaid_edges;
    uint64_t max_image_pixels;
    uint64_t max_remote_image_bytes;
    size_t max_image_resources;
    size_t max_concurrent_downloads;
    void *uia_provider;
    int hovered_code_block;
    int hovered_mermaid_block;
    bool tracking_mouse;
    bool document_copy_button_enabled;
    TintaNoticeKind notice_kind;
    int notice_code_block;
    int notice_mermaid_block;

    bool selecting;
    size_t selection_anchor;
    size_t selection_focus;
    int mouse_x;
    int mouse_y;
    bool scrollbar_hovered;
    bool scrollbar_dragging;
    float scrollbar_drag_start_y;
    float scrollbar_drag_start_scroll;
    bool h_scrollbar_hovered;
    bool h_scrollbar_dragging;
    float h_scrollbar_drag_start_x;
    float h_scrollbar_drag_start_scroll;
    int hovered_horizontal_region;
    int dragging_horizontal_region;
    float horizontal_region_drag_start_x;
    float horizontal_region_drag_start_scroll;

    size_t parse_time_us;
    size_t ast_node_count;
    size_t layout_time_us;
    size_t draw_calls;
};

COLORREF tinta_color(uint32_t rgb);
bool tinta_utf8_to_utf16(const char *text, size_t length, TintaStr16 *output);
bool tinta_utf16_to_utf8(const wchar_t *text, size_t length, TintaStr8 *output);
wchar_t *tinta_wcsdup_n(const wchar_t *text, size_t length);
bool tinta_read_file_bytes(const wchar_t *path, TintaStr8 *output);
bool tinta_set_clipboard_text(HWND owner, const wchar_t *text, size_t length);
bool tinta_get_clipboard_text(HWND owner, TintaStr16 *output);

bool tinta_app_init(TintaApp *app, HINSTANCE instance, const TintaSettings *settings);
void tinta_app_destroy(TintaApp *app);
bool tinta_shared_graphics_initialize(void);
void tinta_shared_graphics_uninitialize(void);
bool tinta_app_create_device(TintaApp *app);
void tinta_app_discard_device(TintaApp *app);
bool tinta_app_update_formats(TintaApp *app);
void tinta_draw_text_layout(TintaApp *app, D2D1_POINT_2F origin,
                            IDWriteTextLayout *layout, ID2D1Brush *brush,
                            D2D1_DRAW_TEXT_OPTIONS options);
bool tinta_app_load_source(TintaApp *app, const char *source, size_t length,
                           const wchar_t *path);
bool tinta_app_update_source(TintaApp *app, const char *source, size_t length,
                             const wchar_t *path, bool new_document);
bool tinta_app_prepare_source(const char *source, size_t length,
                              const wchar_t *path, size_t max_nodes,
                              size_t max_depth, TintaPreparedSource *prepared);
void tinta_app_destroy_prepared_source(TintaPreparedSource *prepared);
void tinta_app_commit_prepared_source(TintaApp *app,
                                      TintaPreparedSource *prepared,
                                      bool new_document);
void tinta_app_clear_image_resources(TintaApp *app);
void tinta_app_invalidate_image_requests(TintaApp *app);
#if TINTA_ENABLE_MERMAID
void tinta_app_clear_mermaid_cache(TintaApp *app);
const TintaMermaidParseResult *tinta_app_mermaid_parse(
    TintaApp *app, const TintaElement *element,
    const char *source, size_t length);
#endif

void tinta_layout_clear(TintaApp *app);
bool tinta_layout_document(TintaApp *app);
bool tinta_layout_document_viewport_first(TintaApp *app);
bool tinta_layout_continue(TintaApp *app);
void tinta_render(TintaApp *app);
void tinta_scroll(TintaApp *app, float amount);
bool tinta_vertical_scrollbar_geometry(const TintaApp *app,
                                       TintaScrollbarGeometry *geometry);
bool tinta_horizontal_scrollbar_geometry(const TintaApp *app,
                                         TintaScrollbarGeometry *geometry);
bool tinta_scrollbar_update_hover(TintaApp *app, int x, int y);
bool tinta_scrollbar_begin_drag(TintaApp *app, int x, int y);
bool tinta_scrollbar_drag(TintaApp *app, int x, int y);
bool tinta_scrollbar_end_drag(TintaApp *app, int x, int y);
bool tinta_horizontal_region_scroll_at(TintaApp *app, int x, int y,
                                       float amount);
bool tinta_horizontal_region_scroll_run_into_view(TintaApp *app,
                                                   const TintaTextRun *run);
void tinta_horizontal_region_clear_states(TintaApp *app);
bool tinta_collapsible_header_at(const TintaApp *app, int x, int y);
bool tinta_toggle_collapsible_at(TintaApp *app, int x, int y, bool animate);
bool tinta_block_animation_tick(TintaApp *app, ULONGLONG now);
bool tinta_block_animation_active(const TintaApp *app);
bool tinta_expand_run_block(TintaApp *app, const TintaTextRun *run);
bool tinta_run_is_visually_exposed(const TintaApp *app,
                                   const TintaTextRun *run);
void tinta_hit_test(TintaApp *app, float x, float y, size_t *position, const char **url);
bool tinta_text_at(TintaApp *app, float x, float y, const char **url);
bool tinta_copy_selection(TintaApp *app);
bool tinta_copy_code_at(TintaApp *app, int x, int y, bool *copied);
bool tinta_copy_mermaid_at(TintaApp *app, int x, int y, bool *copied);
bool tinta_copy_document_at(TintaApp *app, int x, int y, bool *copied);
bool tinta_document_button_at(const TintaApp *app, int x, int y);
int tinta_code_block_at(const TintaApp *app, int x, int y);
bool tinta_code_button_at(const TintaApp *app, int x, int y);
int tinta_mermaid_block_at(const TintaApp *app, int x, int y);
bool tinta_mermaid_button_at(const TintaApp *app, int x, int y);
bool tinta_jump_to_internal_link(TintaApp *app, const char *url);

bool tinta_image_resource_get(TintaApp *app, const char *url,
                              size_t *resource_index, bool *ready);
bool tinta_remote_image_complete(TintaApp *app);

#ifdef __cplusplus
}
#endif

#endif
