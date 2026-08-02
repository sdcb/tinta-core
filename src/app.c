#include "app.h"

#include <commdlg.h>
#include <limits.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <urlmon.h>

typedef struct RemoteWork {
    TintaApp *app;
    size_t index;
    LONG generation;
    wchar_t *url;
} RemoteWork;

typedef struct RemoteResult {
    size_t index;
    LONG generation;
    BYTE *pixels;
    UINT width;
    UINT height;
    UINT stride;
    UINT buffer_size;
    bool success;
} RemoteResult;

static void remote_result_destroy(RemoteResult *result) {
    if (!result) return;
    free(result->pixels);
    free(result);
}

static bool remote_result_enqueue(TintaApp *app, RemoteResult *result) {
    bool queued = false;
    AcquireSRWLockExclusive(&app->remote_results_lock);
    if (!InterlockedCompareExchange(&app->closing, 0, 0))
        queued = tinta_vec_push(&app->remote_results, &result) != NULL;
    ReleaseSRWLockExclusive(&app->remote_results_lock);
    return queued;
}

static RemoteResult *remote_result_dequeue(TintaApp *app) {
    RemoteResult *result = NULL;
    AcquireSRWLockExclusive(&app->remote_results_lock);
    if (app->remote_results.len) {
        result = TINTA_VEC_AT(RemoteResult *, app->remote_results, 0);
        app->remote_results.len--;
        if (app->remote_results.len)
            TINTA_VEC_AT(RemoteResult *, app->remote_results, 0) =
                TINTA_VEC_AT(RemoteResult *, app->remote_results,
                             app->remote_results.len);
    }
    ReleaseSRWLockExclusive(&app->remote_results_lock);
    return result;
}

static bool remote_result_remove(TintaApp *app, RemoteResult *result) {
    size_t index;
    bool removed = false;
    AcquireSRWLockExclusive(&app->remote_results_lock);
    for (index = 0; index < app->remote_results.len; index++) {
        if (TINTA_VEC_AT(RemoteResult *, app->remote_results, index) == result) {
            app->remote_results.len--;
            if (index < app->remote_results.len)
                TINTA_VEC_AT(RemoteResult *, app->remote_results, index) =
                    TINTA_VEC_AT(RemoteResult *, app->remote_results,
                                 app->remote_results.len);
            removed = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&app->remote_results_lock);
    return removed;
}

static void remote_results_clear(TintaApp *app) {
    RemoteResult *result;
    while ((result = remote_result_dequeue(app)) != NULL)
        remote_result_destroy(result);
}

typedef struct TintaBindStatusCallback {
    IBindStatusCallback iface;
    LONG references;
    volatile LONG *closing;
    volatile LONG *current_generation;
    LONG expected_generation;
} TintaBindStatusCallback;

static TintaBindStatusCallback *bind_callback_from_interface(
    IBindStatusCallback *value) {
    return CONTAINING_RECORD(value, TintaBindStatusCallback, iface);
}

static HRESULT STDMETHODCALLTYPE bind_query_interface(IBindStatusCallback *self,
    REFIID iid, void **result) {
    if (!result) return E_POINTER;
    *result = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) &&
        !IsEqualIID(iid, &IID_IBindStatusCallback)) return E_NOINTERFACE;
    *result = self;
    InterlockedIncrement(&bind_callback_from_interface(self)->references);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE bind_add_ref(IBindStatusCallback *self) {
    return (ULONG)InterlockedIncrement(
        &bind_callback_from_interface(self)->references);
}

static ULONG STDMETHODCALLTYPE bind_release(IBindStatusCallback *self) {
    return (ULONG)InterlockedDecrement(
        &bind_callback_from_interface(self)->references);
}

static HRESULT STDMETHODCALLTYPE bind_start(IBindStatusCallback *self,
    DWORD reserved, IBinding *binding) {
    (void)self; (void)reserved; (void)binding; return S_OK;
}

static HRESULT STDMETHODCALLTYPE bind_priority(IBindStatusCallback *self,
    LONG *priority) {
    (void)self; (void)priority; return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE bind_low_resource(IBindStatusCallback *self,
    DWORD reserved) {
    (void)self; (void)reserved; return S_OK;
}

static HRESULT STDMETHODCALLTYPE bind_progress(IBindStatusCallback *self,
    ULONG progress, ULONG maximum, ULONG status, LPCWSTR text) {
    TintaBindStatusCallback *callback = bind_callback_from_interface(self);
    (void)progress; (void)maximum; (void)status; (void)text;
    return InterlockedCompareExchange(callback->closing, 0, 0) ||
           InterlockedCompareExchange(callback->current_generation, 0, 0) !=
               callback->expected_generation ? E_ABORT : S_OK;
}

static HRESULT STDMETHODCALLTYPE bind_stop(IBindStatusCallback *self,
    HRESULT result, LPCWSTR text) {
    (void)self; (void)result; (void)text; return S_OK;
}

static HRESULT STDMETHODCALLTYPE bind_info(IBindStatusCallback *self,
    DWORD *flags, BINDINFO *info) {
    (void)self;
    if (!flags || !info) return E_POINTER;
    *flags = BINDF_GETNEWESTVERSION;
    info->dwBindVerb = BINDVERB_GET;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bind_data(IBindStatusCallback *self,
    DWORD flags, DWORD size, FORMATETC *format, STGMEDIUM *medium) {
    (void)self; (void)flags; (void)size; (void)format; (void)medium; return S_OK;
}

static HRESULT STDMETHODCALLTYPE bind_object(IBindStatusCallback *self,
    REFIID iid, IUnknown *object) {
    (void)self; (void)iid; (void)object; return E_NOTIMPL;
}

static IBindStatusCallbackVtbl bind_status_vtable = {
    bind_query_interface, bind_add_ref, bind_release, bind_start,
    bind_priority, bind_low_resource, bind_progress, bind_stop,
    bind_info, bind_data, bind_object
};

static void release_unknown(IUnknown **value) {
    if (*value) { IUnknown_Release(*value); *value = NULL; }
}

static void release_text_format(IDWriteTextFormat **value) {
    if (*value) { (*value)->lpVtbl->Release(*value); *value = NULL; }
}

static void remote_images_clear(TintaApp *app) {
    size_t i;
    for (i = 0; i < app->remote_images.len; i++) {
        TintaRemoteImage *image = TINTA_VEC_PTR(
            TintaRemoteImage, app->remote_images, i);
        free(image->url);
        if (image->source) IWICBitmapSource_Release(image->source);
    }
    tinta_vec_clear(&app->remote_images);
}

static IDWriteTextFormat *create_format(TintaApp *app, const wchar_t *family,
                                        float size, TintaDWriteFontWeight weight,
                                        TintaDWriteFontStyle style) {
    IDWriteTextFormat *format = NULL;
    HRESULT hr = app->dwrite_factory->lpVtbl->CreateTextFormat(
        app->dwrite_factory, family, NULL, weight, style,
        TINTA_DWRITE_FONT_STRETCH_NORMAL,
        size * app->dpi_scale * app->zoom, L"en-us", &format);
    if (SUCCEEDED(hr) && format)
        format->lpVtbl->SetParagraphAlignment(
            format, TINTA_DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (SUCCEEDED(hr) && format)
        format->lpVtbl->SetWordWrapping(format, TINTA_DWRITE_WORD_WRAPPING_NO_WRAP);
    return SUCCEEDED(hr) ? format : NULL;
}

static void create_font_fallback(TintaApp *app) {
    IDWriteFactory2 *factory2 = NULL;
    IDWriteFontFallbackBuilder *builder = NULL;
    IDWriteFontFallback *system_fallback = NULL;
    const WCHAR *jp_families[] = {L"Yu Gothic UI", L"Meiryo", L"Microsoft YaHei UI"};
    const WCHAR *kr_families[] = {L"Malgun Gothic", L"Microsoft YaHei UI", L"Yu Gothic UI"};
    const WCHAR *cjk_families[] = {L"Microsoft YaHei UI", L"Yu Gothic UI", L"Meiryo", L"Malgun Gothic"};
    const WCHAR *emoji_families[] = {L"Segoe UI Emoji", L"Segoe UI Symbol"};
    TintaDWriteUnicodeRange jp_ranges[] = {
        {0x3040, 0x309f}, {0x30a0, 0x30ff}, {0x3190, 0x319f},
        {0x31f0, 0x31ff}, {0x32d0, 0x32ff}, {0x3300, 0x3357},
        {0xff65, 0xff9f}
    };
    TintaDWriteUnicodeRange kr_ranges[] = {
        {0x1100, 0x11ff}, {0x3130, 0x318f}, {0x3200, 0x321e},
        {0x3260, 0x327f}, {0xac00, 0xd7af}
    };
    TintaDWriteUnicodeRange cjk_ranges[] = {
        {0x2e80, 0x303f}, {0x3100, 0x312f}, {0x31a0, 0x31ef},
        {0x3220, 0x325f}, {0x3280, 0x32cf}, {0x3358, 0x33ff},
        {0x3400, 0x4dbf}, {0x4dc0, 0x4dff}, {0x4e00, 0x9fff},
        {0xf900, 0xfaff}, {0xfe10, 0xfe1f}, {0xfe30, 0xfe4f},
        {0xff00, 0xff64}, {0xffa0, 0xffef}, {0x20000, 0x2fa1f}
    };
    TintaDWriteUnicodeRange full_range = {0, 0x10ffff};
    if (FAILED(app->dwrite_factory->lpVtbl->QueryInterface(
            app->dwrite_factory, &TINTA_IID_IDWRITE_FACTORY2,
            (void **)&factory2))) return;
    if (SUCCEEDED(factory2->lpVtbl->CreateFontFallbackBuilder(factory2, &builder))) {
        builder->lpVtbl->AddMapping(builder, jp_ranges, 7, jp_families, 3,
                                    NULL, NULL, NULL, 1.0f);
        builder->lpVtbl->AddMapping(builder, kr_ranges, 5, kr_families, 3,
                                    NULL, NULL, NULL, 1.0f);
        builder->lpVtbl->AddMapping(builder, cjk_ranges, 15, cjk_families, 4,
                                    NULL, NULL, NULL, 1.0f);
        builder->lpVtbl->AddMapping(builder, &full_range, 1, emoji_families, 2,
                                    NULL, NULL, NULL, 1.0f);
        if (SUCCEEDED(factory2->lpVtbl->GetSystemFontFallback(
                factory2, &system_fallback))) {
            builder->lpVtbl->AddMappings(builder, system_fallback);
            system_fallback->lpVtbl->Release(system_fallback);
        }
        builder->lpVtbl->CreateFontFallback(builder, &app->font_fallback);
        builder->lpVtbl->Release(builder);
    }
    factory2->lpVtbl->Release(factory2);
}

static void configure_text_rendering(TintaApp *app) {
    IDWriteRenderingParams *defaults = NULL;
    IDWriteRenderingParams *custom = NULL;
    app->render_target->lpVtbl->SetTextAntialiasMode(
        app->render_target, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    if (SUCCEEDED(app->dwrite_factory->lpVtbl->CreateRenderingParams(
            app->dwrite_factory, &defaults))) {
        app->dwrite_factory->lpVtbl->CreateCustomRenderingParams(
            app->dwrite_factory, defaults->lpVtbl->GetGamma(defaults),
            defaults->lpVtbl->GetEnhancedContrast(defaults), 1.0f,
            defaults->lpVtbl->GetPixelGeometry(defaults),
            TINTA_DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &custom);
        defaults->lpVtbl->Release(defaults);
    }
    if (custom) {
        app->render_target->lpVtbl->SetTextRenderingParams(
            app->render_target, custom);
        custom->lpVtbl->Release(custom);
    }
}

bool tinta_app_update_formats(TintaApp *app) {
    size_t i;
    static const float heading_sizes[6] = {32, 26, 22, 18, 16, 14};
    IDWriteTextFormat *body = NULL;
    IDWriteTextFormat *bold = NULL;
    IDWriteTextFormat *italic = NULL;
    IDWriteTextFormat *small_text = NULL;
    IDWriteTextFormat *code = NULL;
    IDWriteTextFormat *ui = NULL;
    IDWriteTextFormat *headings[6] = {0};
    body = create_format(app, app->theme->font_family, 16,
                         TINTA_DWRITE_FONT_WEIGHT_NORMAL,
                         TINTA_DWRITE_FONT_STYLE_NORMAL);
    bold = create_format(app, app->theme->font_family, 16,
                         TINTA_DWRITE_FONT_WEIGHT_BOLD,
                         TINTA_DWRITE_FONT_STYLE_NORMAL);
    italic = create_format(app, app->theme->font_family, 16,
                           TINTA_DWRITE_FONT_WEIGHT_NORMAL,
                           TINTA_DWRITE_FONT_STYLE_ITALIC);
    small_text = create_format(app, app->theme->font_family, 12,
                               TINTA_DWRITE_FONT_WEIGHT_NORMAL,
                               TINTA_DWRITE_FONT_STYLE_NORMAL);
    code = create_format(app, app->theme->code_font_family, 14,
                         TINTA_DWRITE_FONT_WEIGHT_NORMAL,
                         TINTA_DWRITE_FONT_STYLE_NORMAL);
    ui = create_format(app, app->theme->font_family, 14,
                       TINTA_DWRITE_FONT_WEIGHT_NORMAL,
                       TINTA_DWRITE_FONT_STYLE_NORMAL);
    if (!body || !bold || !italic || !small_text || !code || !ui) goto failed;
    for (i = 0; i < 6; i++)
        if (!(headings[i] = create_format(app, L"Segoe UI", heading_sizes[i],
                                          TINTA_DWRITE_FONT_WEIGHT_BOLD,
                                          TINTA_DWRITE_FONT_STYLE_NORMAL)))
            goto failed;
    release_text_format(&app->body_format);
    release_text_format(&app->bold_format);
    release_text_format(&app->italic_format);
    release_text_format(&app->small_format);
    release_text_format(&app->code_format);
    release_text_format(&app->ui_format);
    for (i = 0; i < 6; i++) release_text_format(&app->heading_formats[i]);
    app->body_format = body;
    app->bold_format = bold;
    app->italic_format = italic;
    app->small_format = small_text;
    app->code_format = code;
    app->ui_format = ui;
    for (i = 0; i < 6; i++) app->heading_formats[i] = headings[i];
    app->format_zoom = app->zoom;
    app->layout_dirty = true;
    return true;
failed:
    release_text_format(&body);
    release_text_format(&bold);
    release_text_format(&italic);
    release_text_format(&small_text);
    release_text_format(&code);
    release_text_format(&ui);
    for (i = 0; i < 6; i++) release_text_format(&headings[i]);
    return false;
}

bool tinta_app_create_device(TintaApp *app) {
    D2D1_SIZE_U size;
    HRESULT hr;
    if (!app->hwnd || !app->d2d_factory) return false;
    size.width = (UINT32)(app->width > 0 ? app->width : 1);
    size.height = (UINT32)(app->height > 0 ? app->height : 1);
    if (app->render_target) {
        hr = app->render_target->lpVtbl->Resize(app->render_target, &size);
        if (SUCCEEDED(hr)) return true;
        tinta_app_discard_device(app);
    }
    {
        D2D1_RENDER_TARGET_PROPERTIES properties;
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_properties;
        D2D1_COLOR_F white = {1, 1, 1, 1};
        memset(&properties, 0, sizeof(properties));
        properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        properties.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
        properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
        properties.dpiX = 96.0f;
        properties.dpiY = 96.0f;
        properties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        hwnd_properties.hwnd = app->hwnd;
        hwnd_properties.pixelSize = size;
        hwnd_properties.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY;
        hr = app->d2d_factory->lpVtbl->CreateHwndRenderTarget(
            app->d2d_factory, &properties, &hwnd_properties, &app->render_target);
        if (FAILED(hr)) return false;
        hr = app->render_target->lpVtbl->CreateSolidColorBrush(
            app->render_target, &white, NULL, &app->brush);
        if (FAILED(hr)) { tinta_app_discard_device(app); return false; }
        app->render_target->lpVtbl->QueryInterface(
            app->render_target, &TINTA_IID_ID2D1_DEVICE_CONTEXT,
            (void **)&app->device_context);
        configure_text_rendering(app);
    }
    return true;
}

void tinta_app_discard_device(TintaApp *app) {
    size_t i;
    for (i = 0; i < app->bitmaps.len; i++) {
        TintaDrawBitmap *item = TINTA_VEC_PTR(TintaDrawBitmap, app->bitmaps, i);
        if (item->bitmap) { item->bitmap->lpVtbl->Release(item->bitmap); item->bitmap = NULL; }
    }
    if (app->brush) { app->brush->lpVtbl->Release(app->brush); app->brush = NULL; }
    if (app->device_context) {
        app->device_context->lpVtbl->Release(app->device_context);
        app->device_context = NULL;
    }
    if (app->render_target) {
        app->render_target->lpVtbl->Release(app->render_target);
        app->render_target = NULL;
    }
}

void tinta_draw_text_layout(TintaApp *app, D2D1_POINT_2F origin,
                            IDWriteTextLayout *layout, ID2D1Brush *brush,
                            D2D1_DRAW_TEXT_OPTIONS options) {
    ID2D1HwndRenderTarget *target = app->device_context ?
        app->device_context : app->render_target;
    if (!target || !layout || !brush) return;
    target->lpVtbl->DrawTextLayout(target, origin, layout, brush, options);
}


bool tinta_app_init(TintaApp *app, HINSTANCE instance, const TintaSettings *settings) {
    HRESULT hr;
    memset(app, 0, sizeof(*app));
    app->instance = instance;
    app->width = settings->width;
    app->height = settings->height;
    app->theme_index = settings->theme_index;
    app->theme = &TINTA_THEMES[app->theme_index];
    app->zoom = settings->zoom;
    app->layout_dirty = true;
    app->hovered_code_block = -1;
    tinta_str8_init(&app->source);
    tinta_str16_init(&app->current_file);
    tinta_str16_init(&app->doc_text);
    tinta_str16_init(&app->search_query);
    tinta_vec_init(&app->text_runs, sizeof(TintaTextRun));
    tinta_vec_init(&app->rects, sizeof(TintaDrawRect));
    tinta_vec_init(&app->lines, sizeof(TintaDrawLine));
    tinta_vec_init(&app->bitmaps, sizeof(TintaDrawBitmap));
    tinta_vec_init(&app->code_blocks, sizeof(TintaCodeBlock));
    tinta_vec_init(&app->headings, sizeof(TintaHeading));
    tinta_vec_init(&app->scroll_anchors, sizeof(TintaScrollAnchor));
    tinta_vec_init(&app->hit_entries, sizeof(TintaHitEntry));
    app->hit_index_dirty = true;
    tinta_vec_init(&app->remote_images, sizeof(TintaRemoteImage));
    tinta_vec_init(&app->worker_handles, sizeof(HANDLE));
    InitializeSRWLock(&app->remote_results_lock);
    tinta_vec_init(&app->remote_results, sizeof(RemoteResult *));
    tinta_vec_init(&app->viewer_search_matches, sizeof(TintaSearchMatch));
    app->viewer_search_index = -1;
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
    app->com_initialized = SUCCEEDED(hr);
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           &IID_ID2D1Factory, NULL, (void **)&app->d2d_factory);
    if (FAILED(hr)) {
        tinta_app_destroy(app);
        return false;
    }
    hr = DWriteCreateFactory(TINTA_DWRITE_FACTORY_TYPE_SHARED,
                             &TINTA_IID_IDWRITE_FACTORY,
                             (IUnknown **)&app->dwrite_factory);
    if (FAILED(hr)) {
        tinta_app_destroy(app);
        return false;
    }
    create_font_fallback(app);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&app->wic_factory);
    if (FAILED(hr)) {
        tinta_app_destroy(app);
        return false;
    }
    return true;
}

void tinta_app_destroy(TintaApp *app) {
    size_t i;
    if (!app) return;
    InterlockedExchange(&app->closing, 1);
    for (i = 0; i < app->worker_handles.len; i++) {
        HANDLE handle=TINTA_VEC_AT(HANDLE,app->worker_handles,i);
        WaitForSingleObject(handle,INFINITE); CloseHandle(handle);
    }
    remote_results_clear(app);
    remote_images_clear(app);
    tinta_layout_clear(app);
    tinta_parse_result_destroy(&app->document);
    tinta_vec_destroy(&app->remote_images);
    tinta_vec_destroy(&app->worker_handles);
    tinta_vec_destroy(&app->remote_results);
    tinta_vec_destroy(&app->viewer_search_matches);
    tinta_str8_destroy(&app->source);
    tinta_str16_destroy(&app->current_file);
    tinta_str16_destroy(&app->doc_text);
    tinta_str16_destroy(&app->search_query);
    tinta_vec_destroy(&app->text_runs);
    tinta_vec_destroy(&app->rects);
    tinta_vec_destroy(&app->lines);
    tinta_vec_destroy(&app->bitmaps);
    tinta_vec_destroy(&app->code_blocks);
    tinta_vec_destroy(&app->headings);
    tinta_vec_destroy(&app->scroll_anchors);
    tinta_vec_destroy(&app->hit_entries);
    for (i = 0; i < 6; i++) release_text_format(&app->heading_formats[i]);
    release_text_format(&app->body_format);
    release_text_format(&app->bold_format);
    release_text_format(&app->italic_format);
    release_text_format(&app->small_format);
    release_text_format(&app->code_format);
    release_text_format(&app->ui_format);
    tinta_app_discard_device(app);
    release_unknown((IUnknown **)&app->wic_factory);
    if (app->font_fallback) app->font_fallback->lpVtbl->Release(app->font_fallback);
    if (app->dwrite_factory) app->dwrite_factory->lpVtbl->Release(app->dwrite_factory);
    if (app->d2d_factory) app->d2d_factory->lpVtbl->Release(app->d2d_factory);
    if (app->com_initialized) {
        CoUninitialize();
        app->com_initialized = false;
    }
}

static unsigned __stdcall remote_worker(void *parameter) {
    RemoteWork *work=(RemoteWork *)parameter;
    RemoteResult *result=(RemoteResult *)calloc(1,sizeof(*result));
    wchar_t cache[MAX_PATH*2];
    HRESULT com_result = CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    TintaBindStatusCallback callback;
    memset(&callback, 0, sizeof(callback));
    callback.iface.lpVtbl = &bind_status_vtable;
    callback.references = 1;
    callback.closing = &work->app->closing;
    callback.current_generation = &work->app->document_generation;
    callback.expected_generation = work->generation;
    if(result) {
        IWICImagingFactory *wic = NULL;
        IWICBitmapDecoder *decoder = NULL;
        IWICBitmapFrameDecode *frame = NULL;
        IWICFormatConverter *converter = NULL;
        result->index=work->index;
        result->generation=work->generation;
        if (SUCCEEDED(URLDownloadToCacheFileW(NULL,work->url,cache,
                                             MAX_PATH*2,0,&callback.iface)) &&
            SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL,
                                       CLSCTX_INPROC_SERVER,
                                       &IID_IWICImagingFactory,
                                       (void **)&wic)) &&
            SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(
                wic, cache, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                &decoder)) &&
            SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) &&
            SUCCEEDED(IWICBitmapFrameDecode_GetSize(
                frame, &result->width, &result->height)) &&
            result->width && result->height && result->width <= UINT_MAX / 4) {
            result->stride = result->width * 4;
            if (result->height <= UINT_MAX / result->stride) {
                result->buffer_size = result->stride * result->height;
                result->pixels = (BYTE *)malloc(result->buffer_size);
            }
            if (result->pixels &&
                SUCCEEDED(IWICImagingFactory_CreateFormatConverter(
                    wic, &converter)) &&
                SUCCEEDED(IWICFormatConverter_Initialize(
                    converter, (IWICBitmapSource *)frame,
                    &GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone, NULL, 0,
                    WICBitmapPaletteTypeCustom)) &&
                SUCCEEDED(IWICBitmapSource_CopyPixels(
                    (IWICBitmapSource *)converter, NULL, result->stride,
                    result->buffer_size, result->pixels)))
                result->success = true;
        }
        if (converter) IWICFormatConverter_Release(converter);
        if (frame) IWICBitmapFrameDecode_Release(frame);
        if (decoder) IWICBitmapDecoder_Release(decoder);
        if (wic) IWICImagingFactory_Release(wic);
    }
    /* The message is only a wake-up; the app-owned queue holds the result. */
    if (!result || !remote_result_enqueue(work->app, result)) {
        remote_result_destroy(result);
    } else if (!PostMessageW(work->app->hwnd,TINTA_WM_IMAGE_READY,0,0) &&
               remote_result_remove(work->app, result)) {
        remote_result_destroy(result);
    }
    free(work->url); free(work);
    if (SUCCEEDED(com_result)) CoUninitialize();
    return 0;
}

static void reap_worker_handles(TintaApp *app) {
    size_t index = 0;
    while (index < app->worker_handles.len) {
        HANDLE handle = TINTA_VEC_AT(HANDLE, app->worker_handles, index);
        if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
            CloseHandle(handle);
            app->worker_handles.len--;
            if (index < app->worker_handles.len)
                TINTA_VEC_AT(HANDLE, app->worker_handles, index) =
                    TINTA_VEC_AT(HANDLE, app->worker_handles,
                                 app->worker_handles.len);
        } else {
            index++;
        }
    }
}

static IWICBitmapSource *remote_image_request_failed(TintaApp *app,
                                                     size_t image_index,
                                                     bool *failed) {
    TINTA_VEC_AT(TintaRemoteImage, app->remote_images, image_index).state = -1;
    if (failed) *failed = true;
    return NULL;
}

IWICBitmapSource *tinta_remote_image_request(TintaApp *app, const char *url,
                                             bool *failed) {
    size_t i;
    size_t image_index;
    TintaRemoteImage image = {0};
    RemoteWork *work = NULL;
    uintptr_t thread;
    TintaStr16 wide = {0};
    HANDLE handle;
    if (!app || !url) {
        if (failed) *failed = true;
        return NULL;
    }
    reap_worker_handles(app);
    if (failed) *failed = false;
    for (i = 0; i < app->remote_images.len; i++) {
        TintaRemoteImage *cached = TINTA_VEC_PTR(
            TintaRemoteImage, app->remote_images, i);
        if (!strcmp(cached->url, url)) {
            if (failed) *failed = cached->state < 0;
            return cached->state > 0 ? cached->source : NULL;
        }
    }
    if (app->max_concurrent_downloads &&
        app->worker_handles.len >= app->max_concurrent_downloads)
        return NULL;
    image.url = tinta_str8_dup(url, strlen(url));
    if (!image.url || !tinta_vec_push(&app->remote_images, &image)) {
        free(image.url);
        if (failed) *failed = true;
        return NULL;
    }
    image_index = app->remote_images.len - 1;
    if (!tinta_vec_reserve(&app->worker_handles,
                           app->worker_handles.len + 1))
        return remote_image_request_failed(app, image_index, failed);
    work = (RemoteWork *)calloc(1, sizeof(*work));
    if (!work || !tinta_utf8_to_utf16(url, strlen(url), &wide)) {
        free(work);
        tinta_str16_destroy(&wide);
        return remote_image_request_failed(app, image_index, failed);
    }
    work->app = app;
    work->index = image_index;
    work->generation = InterlockedCompareExchange(
        &app->document_generation, 0, 0);
    work->url = tinta_wcsdup_n(wide.data, wide.len);
    tinta_str16_destroy(&wide);
    if (!work->url) {
        free(work);
        return remote_image_request_failed(app, image_index, failed);
    }
    thread = _beginthreadex(NULL, 0, remote_worker, work, 0, NULL);
    if (!thread) {
        free(work->url);
        free(work);
        return remote_image_request_failed(app, image_index, failed);
    }
    handle = (HANDLE)thread;
    tinta_vec_push(&app->worker_handles, &handle);
    return NULL;
}

void tinta_remote_image_complete(TintaApp *app) {
    RemoteResult *result;
    while ((result = remote_result_dequeue(app)) != NULL) {
        if (result->generation == InterlockedCompareExchange(
                &app->document_generation, 0, 0) &&
            result->index < app->remote_images.len) {
            TintaRemoteImage *image = TINTA_VEC_PTR(
                TintaRemoteImage, app->remote_images, result->index);
            IWICBitmap *bitmap = NULL;
            if (result->success && result->pixels &&
                SUCCEEDED(IWICImagingFactory_CreateBitmapFromMemory(
                    app->wic_factory, result->width, result->height,
                    &GUID_WICPixelFormat32bppPBGRA, result->stride,
                    result->buffer_size, result->pixels, &bitmap))) {
                if (image->source) IWICBitmapSource_Release(image->source);
                image->source = (IWICBitmapSource *)bitmap;
                image->state = 1;
            } else {
                image->state = -1;
            }
            app->layout_dirty = true;
            InvalidateRect(app->hwnd, NULL, FALSE);
        }
        remote_result_destroy(result);
    }
    reap_worker_handles(app);
}


bool tinta_app_load_source(TintaApp *app, const char *source, size_t length,
                           const wchar_t *path) {
    TintaStr8 path8 = {0};
    TintaStr8 new_source = {0};
    TintaStr16 new_file = {0};
    TintaParseResult parsed;
    bool focus_mermaid = path && tinta_is_mermaid_document_path_utf16(path);
    if (path && !tinta_utf16_to_utf8(path, wcslen(path), &path8)) return false;
    parsed = tinta_parse_document(source, length, path8.data ? path8.data : "", NULL);
    tinta_str8_destroy(&path8);
    if (!parsed.success) { tinta_parse_result_destroy(&parsed); return false; }
    if (app->max_ast_nodes) {
        TintaVec pending = {0};
        size_t count = 0;
        TintaElement *root = parsed.root;
        bool within_limit = tinta_vec_init(&pending, sizeof(TintaElement *)) &&
                            tinta_vec_push(&pending, &root) != NULL;
        while (within_limit && pending.len) {
            TintaElement *element = TINTA_VEC_AT(
                TintaElement *, pending, pending.len - 1);
            size_t child;
            pending.len--;
            if (++count > app->max_ast_nodes) {
                within_limit = false;
                break;
            }
            for (child = 0; child < element->child_count; child++) {
                TintaElement *value = element->children[child];
                if (!tinta_vec_push(&pending, &value)) {
                    within_limit = false;
                    break;
                }
            }
        }
        tinta_vec_destroy(&pending);
        if (!within_limit) {
            tinta_parse_result_destroy(&parsed);
            return false;
        }
    }
    if (!tinta_str8_assign(&new_source, source, length) ||
        (path && !tinta_str16_assign(&new_file, path, wcslen(path)))) {
        tinta_str8_destroy(&new_source);
        tinta_str16_destroy(&new_file);
        tinta_parse_result_destroy(&parsed);
        return false;
    }
    tinta_parse_result_destroy(&app->document);
    app->document = parsed;
    app->parse_time_us = parsed.parse_time_us;
    tinta_str8_destroy(&app->source);
    app->source = new_source;
    tinta_str16_destroy(&app->current_file);
    app->current_file = new_file;
    InterlockedIncrement(&app->document_generation);
    remote_images_clear(app);
    app->layout_dirty = true;
    app->focus_mermaid_on_next_layout = focus_mermaid;
    app->scroll_y = 0;
    app->scroll_x = 0;
    app->selection_anchor = 0;
    app->selection_focus = 0;
    app->viewer_search_index = -1;
    tinta_vec_clear(&app->viewer_search_matches);
    InvalidateRect(app->hwnd, NULL, FALSE);
    return true;
}
