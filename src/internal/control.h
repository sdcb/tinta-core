#ifndef TINTA_INTERNAL_CONTROL_H
#define TINTA_INTERNAL_CONTROL_H

#include "../../include/tinta_core.h"
#include "app.h"
#include "stream_async.h"
#include "uia_provider.h"

typedef struct TintaControl {
    LONG references;
    TintaApp view;
    TintaOptions options;
    TintaLimits limits;
    TintaStr16 base_uri;
    TintaTheme custom_theme;
    wchar_t custom_font[LF_FACESIZE];
    wchar_t custom_code_font[LF_FACESIZE];
    DWORD find_flags;
    bool use_system_theme;
    bool redraw_enabled;
    bool ready_notified;
    TintaStr8 stream_buffer;
    TintaDocumentFormat stream_format;
    UINT stream_refresh_ms;
    ULONGLONG stream_next_due;
    ULONGLONG stream_layout_started;
    uint64_t stream_revision;
    uint64_t stream_layout_revision;
    uint64_t stream_displayed_revision;
    size_t stream_complete_length;
    size_t stream_layout_length;
    size_t stream_displayed_length;
    uint32_t stream_utf8_codepoint;
    uint32_t stream_utf8_minimum;
    unsigned int stream_utf8_expected;
    bool stream_active;
    bool stream_dirty;
    bool stream_parse_in_flight;
    bool stream_layout_in_flight;
    bool stream_ending;
    bool content_update_pending;
    TintaAutoSize auto_size;
    TintaPageMargins page_margins;
    unsigned int notification_depth;
    TintaStreamAsync *stream_async;
    ULONGLONG double_click_tick;
    POINT double_click_point;
    ULONGLONG triple_click_tick;
    POINT triple_click_point;
    size_t line_selection_start;
    size_t line_selection_end;
    size_t line_notified_anchor;
    size_t line_notified_focus;
    bool double_click_armed;
    bool suppress_next_double_click;
    bool line_selecting;
} TintaControl;

LRESULT tinta_control_notify_parent(TintaControl *control, NMHDR *header);
void tinta_control_notify_code(TintaControl *control, UINT code);
void tinta_control_notify_error(TintaControl *control, HRESULT error,
                                const wchar_t *operation,
                                const wchar_t *message);

DWORD tinta_control_default_option_flags(void);
DWORD tinta_control_supported_option_flags(void);
TintaLimits tinta_control_default_limits(void);
bool tinta_control_apply_theme(TintaControl *control, int index);
bool tinta_control_refresh_system_theme(TintaControl *control);
bool tinta_control_set_custom_theme(TintaControl *control,
                                    const TintaThemeSpec *spec);
bool tinta_control_set_zoom(TintaControl *control, float zoom);

bool tinta_control_set_base_uri(TintaControl *control,
                                const wchar_t *base_uri);
bool tinta_control_set_document(TintaControl *control,
                                const char *utf8, size_t length,
                                const wchar_t *base_uri,
                                TintaDocumentFormat format,
                                bool replace_base_uri);
bool tinta_control_set_text(TintaControl *control, const wchar_t *text);
size_t tinta_control_get_text(TintaControl *control, wchar_t *buffer,
                              size_t capacity);
bool tinta_control_apply_auto_size(TintaControl *control);
bool tinta_control_stream_begin(TintaControl *control,
                                const TintaStreamBegin *begin);
bool tinta_control_stream_append(TintaControl *control,
                                 const TintaStreamChunk *chunk);
bool tinta_control_stream_end(TintaControl *control);
bool tinta_control_stream_cancel(TintaControl *control);
bool tinta_control_stream_flush(TintaControl *control, bool force);
void tinta_control_stream_parsed(TintaControl *control);
void tinta_control_stream_stop(TintaControl *control);
void tinta_control_layout_completed(TintaControl *control);
bool tinta_control_resolve_image(TintaApp *app, const char *source,
                                 TintaStr16 *resolved, bool *remote,
                                 bool *blocked);
wchar_t *tinta_control_resolve_link(TintaControl *control, const char *url);

void tinta_control_reset_multi_click(TintaControl *control);
bool tinta_control_find(TintaControl *control,
                        const TintaFindRequest *request);
void tinta_control_find_step(TintaControl *control, int direction);
void tinta_control_clear_find(TintaControl *control);
void tinta_control_activate_link(TintaControl *control, const char *url);
bool tinta_control_handle_input(TintaControl *control, UINT message,
                                WPARAM wparam, LPARAM lparam,
                                LRESULT *result);

LRESULT tinta_control_dispatch_api(TintaControl *control, UINT message,
                                   WPARAM wparam, LPARAM lparam);

#endif
