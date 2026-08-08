#include "internal/control.h"

#include <math.h>
#include <string.h>

DWORD tinta_control_default_option_flags(void) {
    DWORD flags = TINTA_OPTION_SELECTION |
                  TINTA_OPTION_KEYBOARD_NAVIGATION |
                  TINTA_OPTION_OPEN_UNHANDLED_LINKS |
                  TINTA_OPTION_MOUSE_ZOOM;
#if TINTA_ENABLE_REMOTE_IMAGES
    flags |= TINTA_OPTION_REMOTE_IMAGES;
#endif
#if TINTA_ENABLE_LOCAL_IMAGES
    flags |= TINTA_OPTION_LOCAL_IMAGES;
#endif
    return flags;
}

DWORD tinta_control_supported_option_flags(void) {
    DWORD flags = TINTA_OPTION_SELECTION |
                  TINTA_OPTION_KEYBOARD_NAVIGATION |
                  TINTA_OPTION_OPEN_UNHANDLED_LINKS |
                  TINTA_OPTION_MOUSE_ZOOM |
                  TINTA_OPTION_DOCUMENT_COPY_BUTTON;
#if TINTA_ENABLE_REMOTE_IMAGES
    flags |= TINTA_OPTION_REMOTE_IMAGES;
#endif
#if TINTA_ENABLE_LOCAL_IMAGES
    flags |= TINTA_OPTION_LOCAL_IMAGES;
#endif
    return flags;
}

TintaLimits tinta_control_default_limits(void) {
    TintaLimits limits;
    memset(&limits, 0, sizeof(limits));
    limits.cb_size = sizeof(limits);
    limits.max_document_bytes = 64u * 1024u * 1024u;
    limits.max_ast_nodes = 1000000u;
    limits.max_ast_depth = 256;
    limits.max_mermaid_nodes = 10000u;
    limits.max_mermaid_edges = 20000u;
    limits.max_image_pixels = 64ull * 1024ull * 1024ull;
    limits.max_remote_image_bytes = 64ull * 1024ull * 1024ull;
    limits.max_image_resources = 512;
    limits.max_concurrent_downloads = 4;
    return limits;
}

static bool copy_heading_string(wchar_t *destination, size_t capacity,
                                const wchar_t *source) {
    size_t length = source ? wcslen(source) : 0;
    if (!destination || !capacity) return length == 0;
    if (length >= capacity) {
        destination[0] = 0;
        return false;
    }
    if (length) memcpy(destination, source, length * sizeof(*destination));
    destination[length] = 0;
    return true;
}

LRESULT tinta_control_dispatch_api(TintaControl *control, UINT message,
                                   WPARAM wparam, LPARAM lparam) {
    TintaApp *view = &control->view;
    switch (message) {
        case TMM_SETDOCUMENT: {
            const TintaDocument *document = (const TintaDocument *)lparam;
            if (!document || document->cb_size < sizeof(*document) ||
                document->flags || document->format < TINTA_FORMAT_AUTO ||
                document->format > TINTA_FORMAT_MERMAID)
                return FALSE;
            return tinta_control_set_document(control, document->utf8,
                document->utf8_length, document->base_uri, document->format,
                true);
        }
        case TMM_SETBASEURI:
            if (control->stream_active) return FALSE;
            if (!tinta_control_set_base_uri(
                    control, (const wchar_t *)lparam))
                return FALSE;
            tinta_app_invalidate_image_requests(view);
            tinta_app_clear_image_resources(view);
            view->layout_dirty = true;
            InvalidateRect(view->hwnd, NULL, FALSE);
            return TRUE;
        case TMM_SETOPTIONS: {
            const TintaOptions *options = (const TintaOptions *)lparam;
            DWORD old_flags;
            if (!options || options->cb_size < sizeof(*options)) return FALSE;
            old_flags = control->options.flags;
            control->options = *options;
            control->options.flags &= tinta_control_supported_option_flags();
            if ((old_flags ^ control->options.flags) &
                (TINTA_OPTION_LOCAL_IMAGES | TINTA_OPTION_REMOTE_IMAGES)) {
                tinta_app_invalidate_image_requests(view);
                tinta_app_clear_image_resources(view);
                view->layout_dirty = true;
                InvalidateRect(view->hwnd, NULL, FALSE);
            }
            if (!(control->options.flags & TINTA_OPTION_SELECTION)) {
                view->selection_anchor = view->selection_focus = 0;
                view->selecting = false;
                tinta_control_reset_multi_click(control);
            }
            if ((old_flags ^ control->options.flags) &
                TINTA_OPTION_DOCUMENT_COPY_BUTTON) {
                view->document_copy_button_enabled =
                    (control->options.flags &
                     TINTA_OPTION_DOCUMENT_COPY_BUTTON) != 0;
                if (!view->document_copy_button_enabled &&
                    view->notice_kind == TINTA_NOTICE_COPIED &&
                    view->notice_code_block < 0 &&
                    view->notice_mermaid_block < 0) {
                    KillTimer(view->hwnd, TINTA_TIMER_NOTIFICATION);
                    view->notice_kind = TINTA_NOTICE_NONE;
                    view->notice_code_block = -1;
                    view->notice_mermaid_block = -1;
                }
                InvalidateRect(view->hwnd, NULL, FALSE);
            }
            return TRUE;
        }
        case TMM_GETOPTIONS: {
            TintaOptions *options = (TintaOptions *)lparam;
            if (!options || options->cb_size < sizeof(*options)) return FALSE;
            *options = control->options;
            return TRUE;
        }
        case TMM_SETLIMITS: {
            const TintaLimits *limits = (const TintaLimits *)lparam;
            if (!limits || limits->cb_size < sizeof(*limits) ||
                !limits->max_document_bytes || !limits->max_ast_nodes ||
                !limits->max_ast_depth || !limits->max_mermaid_nodes ||
                !limits->max_mermaid_edges || !limits->max_image_pixels ||
                !limits->max_image_resources ||
                !limits->max_remote_image_bytes ||
                !limits->max_concurrent_downloads)
                return FALSE;
            control->limits = *limits;
            view->max_ast_nodes = limits->max_ast_nodes;
            view->max_ast_depth = limits->max_ast_depth;
            view->max_mermaid_nodes = limits->max_mermaid_nodes;
            view->max_mermaid_edges = limits->max_mermaid_edges;
            view->max_image_pixels = limits->max_image_pixels;
            view->max_remote_image_bytes = limits->max_remote_image_bytes;
            view->max_image_resources = limits->max_image_resources;
            view->max_concurrent_downloads = limits->max_concurrent_downloads;
#if TINTA_ENABLE_MERMAID
            tinta_app_clear_mermaid_cache(view);
#endif
            view->layout_dirty = true;
            InvalidateRect(view->hwnd, NULL, FALSE);
            return TRUE;
        }
        case TMM_GETLIMITS: {
            TintaLimits *limits = (TintaLimits *)lparam;
            if (!limits || limits->cb_size < sizeof(*limits)) return FALSE;
            *limits = control->limits;
            return TRUE;
        }
        case TMM_SETBUILTINTHEME:
            if ((int)wparam == TINTA_THEME_SYSTEM) {
                control->use_system_theme = true;
                return tinta_control_refresh_system_theme(control);
            }
            control->use_system_theme = false;
            return tinta_control_apply_theme(control, (int)wparam);
        case TMM_SETCUSTOMTHEME:
            return tinta_control_set_custom_theme(
                control, (const TintaThemeSpec *)lparam);
        case TMM_SETZOOM:
            return lparam &&
                tinta_control_set_zoom(control, *(const float *)lparam);
        case TMM_GETZOOM:
            if (!lparam) return FALSE;
            *(float *)lparam = view->zoom;
            return TRUE;
        case TMM_SETSCROLLPOS: {
            const TintaScrollPosition *position =
                (const TintaScrollPosition *)lparam;
            if (!position || position->cb_size < sizeof(*position))
                return FALSE;
            view->scroll_x = fmaxf(0, position->x);
            view->scroll_y = fmaxf(0, position->y);
            view->scroll_anchor_pending = false;
            InvalidateRect(view->hwnd, NULL, FALSE);
            tinta_control_notify_code(control, TMN_SCROLLCHANGED);
            return TRUE;
        }
        case TMM_GETSCROLLPOS: {
            TintaScrollPosition *position = (TintaScrollPosition *)lparam;
            if (!position || position->cb_size < sizeof(*position))
                return FALSE;
            position->x = view->scroll_x;
            position->y = view->scroll_y;
            return TRUE;
        }
        case TMM_GETCONTENTSIZE: {
            TintaContentSize *size = (TintaContentSize *)lparam;
            if (!size || size->cb_size < sizeof(*size)) return FALSE;
            size->width = view->content_width;
            size->height = view->content_height;
            return TRUE;
        }
        case TMM_FIND:
            return tinta_control_find(
                control, (const TintaFindRequest *)lparam);
        case TMM_FINDNEXT:
            tinta_control_find_step(control, 1);
            return TRUE;
        case TMM_FINDPREVIOUS:
            tinta_control_find_step(control, -1);
            return TRUE;
        case TMM_CLEARFIND:
            tinta_control_clear_find(control);
            return TRUE;
        case TMM_GETFINDSTATE: {
            TintaFindState *state = (TintaFindState *)lparam;
            if (!state || state->cb_size < sizeof(*state)) return FALSE;
            state->match_count = view->viewer_search_matches.len;
            state->current_index = view->viewer_search_index >= 0 ?
                (size_t)view->viewer_search_index : SIZE_MAX;
            return TRUE;
        }
        case TMM_GETHEADINGCOUNT:
            if ((view->layout_dirty || !view->layout_complete) &&
                !tinta_layout_document(view))
                return 0;
            return (LRESULT)view->headings.len;
        case TMM_GETHEADING: {
            TintaHeadingInfo *info = (TintaHeadingInfo *)lparam;
            TintaHeading *heading;
            if (!info || info->cb_size < sizeof(*info) ||
                info->index >= view->headings.len)
                return FALSE;
            heading = TINTA_VEC_PTR(TintaHeading, view->headings, info->index);
            info->level = heading->level;
            return copy_heading_string(info->text, info->text_capacity,
                                       heading->text) &&
                   copy_heading_string(info->anchor, info->anchor_capacity,
                                       heading->slug);
        }
        case TMM_SCROLLTOHEADING: {
            size_t index = (size_t)wparam;
            if (index >= view->headings.len) return FALSE;
            view->scroll_y = fmaxf(0,
                TINTA_VEC_AT(TintaHeading, view->headings, index).y -
                20.0f * view->dpi_scale);
            InvalidateRect(view->hwnd, NULL, FALSE);
            tinta_control_notify_code(control, TMN_SCROLLCHANGED);
            return TRUE;
        }
        case TMM_SELECTALL:
            view->selection_anchor = 0;
            view->selection_focus = view->doc_text.len;
            InvalidateRect(view->hwnd, NULL, FALSE);
            tinta_control_notify_code(control, TMN_SELECTIONCHANGED);
            tinta_uia_raise_selection_changed(view);
            return TRUE;
        case TMM_CLEARSELECTION:
            view->selection_anchor = view->selection_focus = 0;
            InvalidateRect(view->hwnd, NULL, FALSE);
            tinta_control_notify_code(control, TMN_SELECTIONCHANGED);
            tinta_uia_raise_selection_changed(view);
            return TRUE;
        case TMM_GETSELECTION: {
            TintaSelection *selection = (TintaSelection *)lparam;
            if (!selection || selection->cb_size < sizeof(*selection))
                return FALSE;
            selection->start = min(view->selection_anchor,
                                   view->selection_focus);
            selection->end = max(view->selection_anchor,
                                 view->selection_focus);
            return TRUE;
        }
        case TMM_REFRESHAPPEARANCE:
            return tinta_control_refresh_system_theme(control);
        case TMM_STREAM_BEGIN:
            return tinta_control_stream_begin(
                control, (const TintaStreamBegin *)lparam);
        case TMM_STREAM_APPEND:
            return tinta_control_stream_append(
                control, (const TintaStreamChunk *)lparam);
        case TMM_STREAM_END:
            return tinta_control_stream_end(control);
        case TMM_STREAM_CANCEL:
            return tinta_control_stream_cancel(control);
        case TMM_SETAUTOSIZE: {
            const TintaAutoSize *auto_size = (const TintaAutoSize *)lparam;
            DWORD known_flags = TINTA_AUTOSIZE_HEIGHT |
                                TINTA_AUTOSIZE_MAX_HEIGHT;
            if (!auto_size || auto_size->cb_size < sizeof(*auto_size) ||
                (auto_size->flags & ~known_flags) ||
                ((auto_size->flags & TINTA_AUTOSIZE_MAX_HEIGHT) &&
                 (!(auto_size->flags & TINTA_AUTOSIZE_HEIGHT) ||
                  auto_size->max_height <= 0)) ||
                auto_size->min_height < 0 ||
                ((auto_size->flags & TINTA_AUTOSIZE_MAX_HEIGHT) &&
                 auto_size->max_height < auto_size->min_height))
                return FALSE;
            control->auto_size = *auto_size;
            control->auto_size.cb_size = sizeof(control->auto_size);
            tinta_control_apply_auto_size(control);
            return TRUE;
        }
        case TMM_GETAUTOSIZE: {
            TintaAutoSize *auto_size = (TintaAutoSize *)lparam;
            if (!auto_size || auto_size->cb_size < sizeof(*auto_size))
                return FALSE;
            *auto_size = control->auto_size;
            return TRUE;
        }
        case TMM_GETVERSION: {
            TintaVersionInfo *version = (TintaVersionInfo *)lparam;
            if (!version || version->cb_size < sizeof(*version)) return FALSE;
            version->cb_size = sizeof(*version);
            version->major = TINTA_CORE_VERSION_MAJOR;
            version->minor = TINTA_CORE_VERSION_MINOR;
            version->patch = TINTA_CORE_VERSION_PATCH;
            return TRUE;
        }
        case TMM_GETCAPABILITIES: {
            TintaCapabilities *capabilities = (TintaCapabilities *)lparam;
            DWORD flags = TINTA_CAPABILITY_STREAMING;
            if (!capabilities || capabilities->cb_size < sizeof(*capabilities))
                return FALSE;
#if TINTA_ENABLE_UIA
            flags |= TINTA_CAPABILITY_UIA;
#endif
#if TINTA_ENABLE_MERMAID
            flags |= TINTA_CAPABILITY_MERMAID;
#endif
#if TINTA_ENABLE_SYNTAX
            flags |= TINTA_CAPABILITY_SYNTAX;
#endif
#if TINTA_ENABLE_REMOTE_IMAGES
            flags |= TINTA_CAPABILITY_REMOTE_IMAGES;
#endif
#if TINTA_ENABLE_LOCAL_IMAGES
            flags |= TINTA_CAPABILITY_LOCAL_IMAGES;
#endif
            capabilities->cb_size = sizeof(*capabilities);
            capabilities->flags = flags;
            capabilities->option_flags =
                tinta_control_supported_option_flags();
            return TRUE;
        }
        case TMM_GETSTATS: {
            TintaStats *stats = (TintaStats *)lparam;
            if (!stats || stats->cb_size < sizeof(*stats)) return FALSE;
            stats->cb_size = sizeof(*stats);
            stats->document_revision = view->document_revision;
            stats->source_bytes = view->source.len;
            stats->ast_nodes = view->ast_node_count;
            stats->text_runs = view->text_runs.len;
            stats->image_resources = view->image_resources.len;
            stats->parse_time_us = view->parse_time_us;
            stats->layout_time_us = view->layout_time_us;
            stats->draw_calls = view->draw_calls;
            return TRUE;
        }
        case TMM_SETPAGEMARGINS: {
            const TintaPageMargins *margins =
                (const TintaPageMargins *)lparam;
            if (!margins || margins->cb_size < sizeof(*margins) ||
                !isfinite(margins->left) || margins->left < 0 ||
                !isfinite(margins->top) || margins->top < 0 ||
                !isfinite(margins->right) || margins->right < 0 ||
                !isfinite(margins->bottom) || margins->bottom < 0)
                return FALSE;
            control->page_margins = *margins;
            view->page_margin_left = margins->left;
            view->page_margin_top = margins->top;
            view->page_margin_right = margins->right;
            view->page_margin_bottom = margins->bottom;
            view->layout_dirty = true;
            InvalidateRect(view->hwnd, NULL, FALSE);
            return TRUE;
        }
        case TMM_GETPAGEMARGINS: {
            TintaPageMargins *margins = (TintaPageMargins *)lparam;
            if (!margins || margins->cb_size < sizeof(*margins)) return FALSE;
            *margins = control->page_margins;
            return TRUE;
        }
    }
    return 0;
}
