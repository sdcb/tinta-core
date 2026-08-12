#ifndef TINTA_INTERNAL_EDITOR_H
#define TINTA_INTERNAL_EDITOR_H

#include "../../include/tinta_core.h"
#include "app.h"
#include "editor_document.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TINTA_WM_EDITOR_LAYOUT (WM_APP + 0x30)
#define TINTA_WM_EDITOR_HITTEST (WM_APP + 0x31)
#define TINTA_WM_EDITOR_POSITION_POINT (WM_APP + 0x32)
#define TINTA_TIMER_EDITOR_AUTOSCROLL 0x5001

typedef struct TintaEditorHitTest {
    POINT point;
    size_t position;
} TintaEditorHitTest;

typedef struct TintaEditorPositionPoint {
    size_t position;
    POINT point;
} TintaEditorPositionPoint;

typedef struct TintaEditor {
    HWND hwnd;
    HINSTANCE instance;
    TintaEditorDocument document;
    size_t anchor;
    size_t caret;
    float desired_x;
    float scroll_x;
    float scroll_y;
    int width;
    int height;
    float dpi_scale;
    float padding_left;
    float padding_right;
    float padding_top;
    float padding_bottom;
    float default_line_height;
    float tab_width;
    TintaEditorWrapMode wrap_mode;
    bool style_no_wrap;
    bool allow_vertical_overlay;
    bool allow_horizontal_overlay;
    bool read_only;
    bool redraw_enabled;
    bool focused;
    bool selecting;
    bool line_selecting;
    size_t line_selection_anchor;
    bool pending_layout;
    size_t layout_cursor;
    size_t layout_start;
    bool layout_wrapped;
    uint64_t layout_generation;
    bool pending_high_surrogate;
    wchar_t high_surrogate;
    bool use_system_theme;
    uint32_t background_color;
    uint32_t text_color;
    uint32_t selection_color;
    uint32_t selection_text_color;
    HFONT font;
    bool own_font;
    LOGFONTW log_font;
    ID2D1Factory *d2d_factory;
    IDWriteFactory *dwrite_factory;
    IDWriteFontFallback *font_fallback;
    ID2D1HwndRenderTarget *render_target;
    ID2D1SolidColorBrush *brush;
    IDWriteTextFormat *format;
    bool v_scrollbar_hovered;
    bool h_scrollbar_hovered;
    bool v_scrollbar_dragging;
    bool h_scrollbar_dragging;
    float scrollbar_drag_origin;
    float scrollbar_drag_scroll;
    ULONGLONG last_click_tick;
    POINT last_click_point;
    unsigned int click_count;
    int auto_scroll_x;
    int auto_scroll_y;
    unsigned int undo_group;
    void *uia_provider;
} TintaEditor;

HRESULT tinta_editor_register_class(HINSTANCE instance);
void tinta_editor_unregister_class(HINSTANCE instance);
LRESULT CALLBACK tinta_editor_window_proc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam);

void tinta_control_window_created(void);
void tinta_control_window_destroyed(void);

#if TINTA_ENABLE_UIA
LRESULT tinta_editor_uia_get_object(TintaEditor *editor,
                                    WPARAM wparam, LPARAM lparam);
void tinta_editor_uia_disconnect(TintaEditor *editor);
void tinta_editor_uia_raise_text_changed(TintaEditor *editor);
void tinta_editor_uia_raise_selection_changed(TintaEditor *editor);
void tinta_editor_uia_raise_focus_changed(TintaEditor *editor);
void tinta_editor_uia_raise_scroll_changed(TintaEditor *editor);
void tinta_editor_uia_raise_read_only_changed(TintaEditor *editor,
                                              bool old_value,
                                              bool new_value);
#else
static inline LRESULT tinta_editor_uia_get_object(
    TintaEditor *editor, WPARAM wparam, LPARAM lparam) {
    (void)editor; (void)wparam; (void)lparam; return 0;
}
static inline void tinta_editor_uia_disconnect(TintaEditor *editor) {
    (void)editor;
}
static inline void tinta_editor_uia_raise_text_changed(TintaEditor *editor) {
    (void)editor;
}
static inline void tinta_editor_uia_raise_selection_changed(TintaEditor *editor) {
    (void)editor;
}
static inline void tinta_editor_uia_raise_focus_changed(TintaEditor *editor) {
    (void)editor;
}
static inline void tinta_editor_uia_raise_scroll_changed(TintaEditor *editor) {
    (void)editor;
}
static inline void tinta_editor_uia_raise_read_only_changed(
    TintaEditor *editor, bool old_value, bool new_value) {
    (void)editor; (void)old_value; (void)new_value;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
