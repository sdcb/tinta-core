#include "app.h"
#include "image_cache.h"
#include "features.h"

#include <commdlg.h>
#include <limits.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#if TINTA_ENABLE_REMOTE_IMAGES
#include <process.h>
#include <urlmon.h>
#endif

#if TINTA_ENABLE_REMOTE_IMAGES
typedef struct RemoteWork {
    TintaImageAsync *async;
    size_t index;
    LONG generation;
    wchar_t *url;
    uint64_t max_pixels;
    uint64_t max_download_bytes;
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

struct TintaImageAsync {
    LONG references;
    volatile LONG closing;
    volatile LONG generation;
    volatile LONG active_workers;
    HWND hwnd;
    SRWLOCK results_lock;
    TintaVec results;
};
#endif

static SRWLOCK g_graphics_lock = SRWLOCK_INIT;
static ID2D1Factory *g_d2d_factory;
static IDWriteFactory *g_dwrite_factory;
static IDWriteFontFallback *g_font_fallback;

#if TINTA_ENABLE_REMOTE_IMAGES
static TintaImageAsync *image_async_create(void) {
    TintaImageAsync *async = (TintaImageAsync *)calloc(1, sizeof(*async));
    if (!async) return NULL;
    async->references = 1;
    InitializeSRWLock(&async->results_lock);
    if (!tinta_vec_init(&async->results, sizeof(RemoteResult *))) {
        free(async);
        return NULL;
    }
    return async;
}

static void image_async_add_ref(TintaImageAsync *async) {
    InterlockedIncrement(&async->references);
}

static void remote_result_destroy(RemoteResult *result) {
    if (!result) return;
    free(result->pixels);
    free(result);
}

static bool remote_result_enqueue(TintaImageAsync *async,
                                  RemoteResult *result) {
    bool queued = false;
    AcquireSRWLockExclusive(&async->results_lock);
    if (!InterlockedCompareExchange(&async->closing, 0, 0))
        queued = tinta_vec_push(&async->results, &result) != NULL;
    ReleaseSRWLockExclusive(&async->results_lock);
    return queued;
}

static RemoteResult *remote_result_dequeue(TintaImageAsync *async) {
    RemoteResult *result = NULL;
    AcquireSRWLockExclusive(&async->results_lock);
    if (async->results.len) {
        result = TINTA_VEC_AT(RemoteResult *, async->results, 0);
        async->results.len--;
        if (async->results.len)
            TINTA_VEC_AT(RemoteResult *, async->results, 0) =
                TINTA_VEC_AT(RemoteResult *, async->results,
                             async->results.len);
    }
    ReleaseSRWLockExclusive(&async->results_lock);
    return result;
}

static bool remote_result_remove(TintaImageAsync *async,
                                 RemoteResult *result) {
    size_t index;
    bool removed = false;
    AcquireSRWLockExclusive(&async->results_lock);
    for (index = 0; index < async->results.len; index++) {
        if (TINTA_VEC_AT(RemoteResult *, async->results, index) == result) {
            async->results.len--;
            if (index < async->results.len)
                TINTA_VEC_AT(RemoteResult *, async->results, index) =
                    TINTA_VEC_AT(RemoteResult *, async->results,
                                 async->results.len);
            removed = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&async->results_lock);
    return removed;
}

static void remote_results_clear(TintaImageAsync *async) {
    RemoteResult *result;
    while ((result = remote_result_dequeue(async)) != NULL)
        remote_result_destroy(result);
}

static void image_async_release(TintaImageAsync *async) {
    if (async && !InterlockedDecrement(&async->references)) {
        remote_results_clear(async);
        tinta_vec_destroy(&async->results);
        free(async);
    }
}

static void image_async_close(TintaImageAsync *async) {
    if (!async) return;
    AcquireSRWLockExclusive(&async->results_lock);
    InterlockedExchange(&async->closing, 1);
    async->hwnd = NULL;
    ReleaseSRWLockExclusive(&async->results_lock);
    remote_results_clear(async);
}

static HWND image_async_window(TintaImageAsync *async) {
    HWND hwnd;
    AcquireSRWLockShared(&async->results_lock);
    hwnd = InterlockedCompareExchange(&async->closing, 0, 0) ?
           NULL : async->hwnd;
    ReleaseSRWLockShared(&async->results_lock);
    return hwnd;
}

typedef struct TintaBindStatusCallback {
    IBindStatusCallback iface;
    LONG references;
    volatile LONG *closing;
    volatile LONG *current_generation;
    LONG expected_generation;
    uint64_t max_download_bytes;
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
    (void)maximum; (void)status; (void)text;
    return InterlockedCompareExchange(callback->closing, 0, 0) ||
           InterlockedCompareExchange(callback->current_generation, 0, 0) !=
               callback->expected_generation ||
           (callback->max_download_bytes &&
            progress > callback->max_download_bytes) ? E_ABORT : S_OK;
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
#endif

#if TINTA_ENABLE_IMAGES
static void release_unknown(IUnknown **value) {
    if (*value) { IUnknown_Release(*value); *value = NULL; }
}
#endif

static void release_text_format(IDWriteTextFormat **value) {
    if (*value) { (*value)->lpVtbl->Release(*value); *value = NULL; }
}

void tinta_app_clear_image_resources(TintaApp *app) {
    size_t i;
    for (i = 0; i < app->image_resources.len; i++) {
        TintaImageResource *image = TINTA_VEC_PTR(
            TintaImageResource, app->image_resources, i);
        free(image->key);
        free(image->resolved_uri);
        if (image->source) IWICBitmapSource_Release(image->source);
        if (image->bitmap) image->bitmap->lpVtbl->Release(image->bitmap);
    }
    tinta_vec_clear(&app->image_resources);
}

void tinta_app_invalidate_image_requests(TintaApp *app) {
    if (!app) return;
#if TINTA_ENABLE_REMOTE_IMAGES
    if (app->image_async)
        InterlockedIncrement(&app->image_async->generation);
#endif
}

#if TINTA_ENABLE_MERMAID
void tinta_app_clear_mermaid_cache(TintaApp *app) {
    size_t i;
    if (!app) return;
    for (i = 0; i < app->mermaid_cache.len; i++) {
        TintaMermaidCacheEntry *entry = TINTA_VEC_PTR(
            TintaMermaidCacheEntry, app->mermaid_cache, i);
        tinta_mermaid_parse_result_destroy(&entry->parsed);
    }
    tinta_vec_clear(&app->mermaid_cache);
}

const TintaMermaidParseResult *tinta_app_mermaid_parse(
    TintaApp *app, const TintaElement *element,
    const char *source, size_t length) {
    size_t i;
    TintaMermaidCacheEntry entry;
    if (!app || !element || (!source && length)) return NULL;
    for (i = 0; i < app->mermaid_cache.len; i++) {
        TintaMermaidCacheEntry *cached = TINTA_VEC_PTR(
            TintaMermaidCacheEntry, app->mermaid_cache, i);
        if (cached->element == element) return &cached->parsed;
    }
    memset(&entry, 0, sizeof(entry));
    entry.element = element;
    entry.parsed = tinta_mermaid_parse_limited(source, length,
        app->max_mermaid_nodes, app->max_mermaid_edges);
    if (!tinta_vec_push(&app->mermaid_cache, &entry)) {
        tinta_mermaid_parse_result_destroy(&entry.parsed);
        return NULL;
    }
    return &TINTA_VEC_PTR(TintaMermaidCacheEntry, app->mermaid_cache,
                          app->mermaid_cache.len - 1)->parsed;
}
#endif

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

static void create_font_fallback(IDWriteFactory *dwrite_factory,
                                 IDWriteFontFallback **font_fallback) {
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
    if (FAILED(dwrite_factory->lpVtbl->QueryInterface(
            dwrite_factory, &TINTA_IID_IDWRITE_FACTORY2,
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
        builder->lpVtbl->CreateFontFallback(builder, font_fallback);
        builder->lpVtbl->Release(builder);
    }
    factory2->lpVtbl->Release(factory2);
}

bool tinta_shared_graphics_initialize(void) {
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&g_graphics_lock);
    if (!g_d2d_factory) {
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
            &IID_ID2D1Factory, NULL, (void **)&g_d2d_factory);
        if (SUCCEEDED(hr))
            hr = DWriteCreateFactory(TINTA_DWRITE_FACTORY_TYPE_SHARED,
                &TINTA_IID_IDWRITE_FACTORY,
                (IUnknown **)&g_dwrite_factory);
        if (SUCCEEDED(hr))
            create_font_fallback(g_dwrite_factory, &g_font_fallback);
        if (FAILED(hr)) {
            if (g_font_fallback) {
                g_font_fallback->lpVtbl->Release(g_font_fallback);
                g_font_fallback = NULL;
            }
            if (g_dwrite_factory) {
                g_dwrite_factory->lpVtbl->Release(g_dwrite_factory);
                g_dwrite_factory = NULL;
            }
            if (g_d2d_factory) {
                g_d2d_factory->lpVtbl->Release(g_d2d_factory);
                g_d2d_factory = NULL;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_graphics_lock);
    return SUCCEEDED(hr);
}

void tinta_shared_graphics_uninitialize(void) {
#if TINTA_ENABLE_REMOTE_IMAGES
    tinta_image_cache_uninitialize();
#endif
    AcquireSRWLockExclusive(&g_graphics_lock);
    if (g_font_fallback) {
        g_font_fallback->lpVtbl->Release(g_font_fallback);
        g_font_fallback = NULL;
    }
    if (g_dwrite_factory) {
        g_dwrite_factory->lpVtbl->Release(g_dwrite_factory);
        g_dwrite_factory = NULL;
    }
    if (g_d2d_factory) {
        g_d2d_factory->lpVtbl->Release(g_d2d_factory);
        g_d2d_factory = NULL;
    }
    ReleaseSRWLockExclusive(&g_graphics_lock);
}

static bool acquire_shared_graphics(TintaApp *app) {
    bool result = false;
    AcquireSRWLockShared(&g_graphics_lock);
    if (g_d2d_factory && g_dwrite_factory) {
        g_d2d_factory->lpVtbl->AddRef(g_d2d_factory);
        g_dwrite_factory->lpVtbl->AddRef(g_dwrite_factory);
        if (g_font_fallback)
            g_font_fallback->lpVtbl->AddRef(g_font_fallback);
        app->d2d_factory = g_d2d_factory;
        app->dwrite_factory = g_dwrite_factory;
        app->font_fallback = g_font_fallback;
        result = true;
    }
    ReleaseSRWLockShared(&g_graphics_lock);
    return result;
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
        if (!(headings[i] = create_format(app, app->theme->font_family,
                                          heading_sizes[i],
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
    for (i = 0; i < app->image_resources.len; i++) {
        TintaImageResource *item = TINTA_VEC_PTR(
            TintaImageResource, app->image_resources, i);
        if (item->bitmap) {
            item->bitmap->lpVtbl->Release(item->bitmap);
            item->bitmap = NULL;
        }
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
#if TINTA_ENABLE_IMAGES
    HRESULT hr;
#endif
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
    tinta_vec_init(&app->image_resources, sizeof(TintaImageResource));
#if TINTA_ENABLE_MERMAID
    tinta_vec_init(&app->mermaid_cache, sizeof(TintaMermaidCacheEntry));
#endif
    tinta_vec_init(&app->viewer_search_matches, sizeof(TintaSearchMatch));
    app->viewer_search_index = -1;
#if TINTA_ENABLE_REMOTE_IMAGES
    app->image_async = image_async_create();
    if (!app->image_async) {
        tinta_app_destroy(app);
        return false;
    }
#endif
#if TINTA_ENABLE_IMAGES
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
    app->com_initialized = SUCCEEDED(hr);
#endif
    if (!acquire_shared_graphics(app)) {
        tinta_app_destroy(app);
        return false;
    }
#if TINTA_ENABLE_IMAGES
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&app->wic_factory);
    if (FAILED(hr)) {
        tinta_app_destroy(app);
        return false;
    }
#endif
    return true;
}

void tinta_app_destroy(TintaApp *app) {
    size_t i;
    if (!app) return;
#if TINTA_ENABLE_REMOTE_IMAGES
    image_async_close(app->image_async);
    image_async_release(app->image_async);
    app->image_async = NULL;
#endif
    tinta_app_clear_image_resources(app);
#if TINTA_ENABLE_MERMAID
    tinta_app_clear_mermaid_cache(app);
#endif
    tinta_layout_clear(app);
    tinta_parse_result_destroy(&app->document);
    tinta_vec_destroy(&app->image_resources);
#if TINTA_ENABLE_MERMAID
    tinta_vec_destroy(&app->mermaid_cache);
#endif
    tinta_vec_destroy(&app->viewer_search_matches);
    tinta_str8_destroy(&app->source);
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
#if TINTA_ENABLE_IMAGES
    release_unknown((IUnknown **)&app->wic_factory);
#endif
    if (app->font_fallback) app->font_fallback->lpVtbl->Release(app->font_fallback);
    if (app->dwrite_factory) app->dwrite_factory->lpVtbl->Release(app->dwrite_factory);
    if (app->d2d_factory) app->d2d_factory->lpVtbl->Release(app->d2d_factory);
#if TINTA_ENABLE_IMAGES
    if (app->com_initialized) {
        CoUninitialize();
        app->com_initialized = false;
    }
#endif
}

#if TINTA_ENABLE_REMOTE_IMAGES
static unsigned __stdcall remote_worker(void *parameter) {
    RemoteWork *work=(RemoteWork *)parameter;
    RemoteResult *result=(RemoteResult *)calloc(1,sizeof(*result));
    TintaImageAsync *async = work->async;
    wchar_t cache[MAX_PATH*2];
    HRESULT com_result = CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    TintaBindStatusCallback callback;
    memset(&callback, 0, sizeof(callback));
    callback.iface.lpVtbl = &bind_status_vtable;
    callback.references = 1;
    callback.closing = &async->closing;
    callback.current_generation = &async->generation;
    callback.expected_generation = work->generation;
    callback.max_download_bytes = work->max_download_bytes;
    if(result) {
        IWICImagingFactory *wic = NULL;
        IWICBitmapDecoder *decoder = NULL;
        IWICBitmapFrameDecode *frame = NULL;
        IWICFormatConverter *converter = NULL;
        result->index=work->index;
        result->generation=work->generation;
        WIN32_FILE_ATTRIBUTE_DATA file_info;
        bool downloaded = SUCCEEDED(URLDownloadToCacheFileW(
            NULL, work->url, cache, MAX_PATH * 2, 0, &callback.iface));
        uint64_t file_size = 0;
        if (downloaded && GetFileAttributesExW(
                cache, GetFileExInfoStandard, &file_info))
            file_size = ((uint64_t)file_info.nFileSizeHigh << 32) |
                        file_info.nFileSizeLow;
        else
            downloaded = false;
        if (downloaded &&
            (!work->max_download_bytes ||
             file_size <= work->max_download_bytes) &&
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
            result->width && result->height && result->width <= UINT_MAX / 4 &&
            (!work->max_pixels ||
             (uint64_t)result->width * result->height <= work->max_pixels)) {
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
    InterlockedDecrement(&async->active_workers);
    /* The message is only a wake-up; the async queue owns the result. */
    if (!result || !remote_result_enqueue(async, result)) {
        remote_result_destroy(result);
    } else {
        HWND hwnd = image_async_window(async);
        if ((!hwnd || !PostMessageW(hwnd, TINTA_WM_IMAGE_READY, 0, 0)) &&
            remote_result_remove(async, result))
            remote_result_destroy(result);
    }
    free(work->url); free(work);
    if (SUCCEEDED(com_result)) CoUninitialize();
    image_async_release(async);
    return 0;
}
#endif

#if TINTA_ENABLE_LOCAL_IMAGES
static bool decode_local_image(TintaApp *app, TintaImageResource *image) {
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    HRESULT hr = IWICImagingFactory_CreateDecoderFromFilename(
        app->wic_factory, image->resolved_uri, NULL, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameDecode_GetSize(frame, &image->width, &image->height);
    if (SUCCEEDED(hr) && (!image->width || !image->height ||
        (app->max_image_pixels &&
         (uint64_t)image->width * image->height > app->max_image_pixels)))
        hr = E_FAIL;
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateFormatConverter(
            app->wic_factory, &converter);
    if (SUCCEEDED(hr))
        hr = IWICFormatConverter_Initialize(
            converter, (IWICBitmapSource *)frame,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            NULL, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) {
        image->source = (IWICBitmapSource *)converter;
        converter = NULL;
        image->state = 2;
    } else {
        image->state = -1;
        if (app->resource_error)
            app->resource_error(app, false, image->resolved_uri, hr);
    }
    if (converter) IWICFormatConverter_Release(converter);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    return image->state == 2;
}
#endif

#if TINTA_ENABLE_REMOTE_IMAGES
static bool start_remote_image(TintaApp *app, size_t image_index) {
    TintaImageAsync *async = app->image_async;
    TintaImageResource *image;
    RemoteWork *work;
    uintptr_t thread;
    HANDLE handle;
    if (!async || image_index >= app->image_resources.len) return false;
    image = TINTA_VEC_PTR(TintaImageResource, app->image_resources, image_index);
    if (image->state != 0) return image->state > 0;
    {
        UINT width = 0, height = 0, stride = 0, buffer_size = 0;
        BYTE *pixels = NULL;
        IWICBitmap *bitmap = NULL;
        if (tinta_image_cache_get(image->resolved_uri, &width, &height,
                &stride, &buffer_size, &pixels)) {
            if ((!app->max_image_pixels ||
                 (uint64_t)width * height <= app->max_image_pixels) &&
                SUCCEEDED(IWICImagingFactory_CreateBitmapFromMemory(
                    app->wic_factory, width, height,
                    &GUID_WICPixelFormat32bppPBGRA, stride, buffer_size,
                    pixels, &bitmap))) {
                image->source = (IWICBitmapSource *)bitmap;
                image->width = width;
                image->height = height;
                image->state = 2;
            }
            free(pixels);
            if (image->state == 2) return true;
        }
    }
    if (app->max_concurrent_downloads &&
        (UINT)InterlockedCompareExchange(
            &async->active_workers, 0, 0) >= app->max_concurrent_downloads)
        return true;
    work = (RemoteWork *)calloc(1, sizeof(*work));
    if (!work) {
        image->state = -1;
        if (app->resource_error)
            app->resource_error(app, true, image->resolved_uri, E_OUTOFMEMORY);
        return false;
    }
    work->async = async;
    work->index = image_index;
    work->generation = InterlockedCompareExchange(
        &async->generation, 0, 0);
    work->max_pixels = app->max_image_pixels;
    work->max_download_bytes = app->max_remote_image_bytes;
    work->url = tinta_wcsdup_n(image->resolved_uri,
                               wcslen(image->resolved_uri));
    if (!work->url) {
        free(work);
        image->state = -1;
        if (app->resource_error)
            app->resource_error(app, true, image->resolved_uri, E_OUTOFMEMORY);
        return false;
    }
    image->state = 1;
    AcquireSRWLockExclusive(&async->results_lock);
    async->hwnd = app->hwnd;
    ReleaseSRWLockExclusive(&async->results_lock);
    InterlockedIncrement(&async->active_workers);
    image_async_add_ref(async);
    thread = _beginthreadex(NULL, 0, remote_worker, work, 0, NULL);
    if (!thread) {
        InterlockedDecrement(&async->active_workers);
        image_async_release(async);
        image->state = -1;
        if (app->resource_error)
            app->resource_error(app, true, image->resolved_uri,
                                HRESULT_FROM_WIN32(GetLastError()));
        free(work->url);
        free(work);
        return false;
    }
    handle = (HANDLE)thread;
    CloseHandle(handle);
    return true;
}
#endif

bool tinta_image_resource_get(TintaApp *app, const char *url,
                              size_t *resource_index, bool *ready) {
    size_t i;
    TintaImageResource image = {0};
    TintaStr16 resolved = {0};
    bool remote = false;
    bool blocked = false;
    if (!app || !url || !resource_index || !ready) return false;
    *ready = false;
    for (i = 0; i < app->image_resources.len; i++) {
        TintaImageResource *cached = TINTA_VEC_PTR(
            TintaImageResource, app->image_resources, i);
        if (!strcmp(cached->key, url)) {
#if TINTA_ENABLE_REMOTE_IMAGES
            if (cached->remote && cached->state == 0)
                start_remote_image(app, i);
#endif
            *resource_index = i;
            *ready = cached->state == 2;
            return true;
        }
    }
    if (app->max_image_resources &&
        app->image_resources.len >= app->max_image_resources)
        return true;
    if (app->resolve_image) {
        if (!app->resolve_image(app, url, &resolved, &remote, &blocked))
            return false;
    } else {
        if (!tinta_utf8_to_utf16(url, strlen(url), &resolved)) return false;
        remote = !_strnicmp(url, "http://", 7) ||
                 !_strnicmp(url, "https://", 8);
    }
    image.key = tinta_str8_dup(url, strlen(url));
    image.resolved_uri = resolved.data ?
        tinta_wcsdup_n(resolved.data, resolved.len) : NULL;
    image.remote = remote;
#if !TINTA_ENABLE_REMOTE_IMAGES
    if (remote) blocked = true;
#endif
#if !TINTA_ENABLE_LOCAL_IMAGES
    if (!remote) blocked = true;
#endif
    image.state = blocked || !image.resolved_uri ? -1 : 0;
    tinta_str16_destroy(&resolved);
    if (!image.key || (!blocked && !image.resolved_uri) ||
        !tinta_vec_push(&app->image_resources, &image)) {
        free(image.key);
        free(image.resolved_uri);
        return false;
    }
    i = app->image_resources.len - 1;
    *resource_index = i;
    if (image.state == 0) {
        TintaImageResource *stored = TINTA_VEC_PTR(
            TintaImageResource, app->image_resources, i);
        if (stored->remote) {
#if TINTA_ENABLE_REMOTE_IMAGES
            start_remote_image(app, i);
#endif
        } else {
#if TINTA_ENABLE_LOCAL_IMAGES
            decode_local_image(app, stored);
#endif
        }
        *ready = stored->state == 2;
    }
    return true;
}

bool tinta_remote_image_complete(TintaApp *app) {
#if TINTA_ENABLE_REMOTE_IMAGES
    TintaImageAsync *async;
    RemoteResult *result;
    bool changed = false;
    if (!app || !(async = app->image_async)) return false;
    while ((result = remote_result_dequeue(async)) != NULL) {
        if (result->generation == InterlockedCompareExchange(
                &async->generation, 0, 0) &&
            result->index < app->image_resources.len) {
            TintaImageResource *image = TINTA_VEC_PTR(
                TintaImageResource, app->image_resources, result->index);
            IWICBitmap *bitmap = NULL;
            if (result->success && result->pixels &&
                (!app->max_image_pixels ||
                 (uint64_t)result->width * result->height <=
                    app->max_image_pixels) &&
                SUCCEEDED(IWICImagingFactory_CreateBitmapFromMemory(
                    app->wic_factory, result->width, result->height,
                    &GUID_WICPixelFormat32bppPBGRA, result->stride,
                    result->buffer_size, result->pixels, &bitmap))) {
                tinta_image_cache_put(image->resolved_uri, result->width,
                    result->height, result->stride, result->buffer_size,
                    result->pixels);
                if (image->source) IWICBitmapSource_Release(image->source);
                image->source = (IWICBitmapSource *)bitmap;
                image->width = result->width;
                image->height = result->height;
                image->state = 2;
            } else {
                image->state = -1;
                if (app->resource_error)
                    app->resource_error(app, true, image->resolved_uri,
                                        E_FAIL);
            }
            app->layout_dirty = true;
            InvalidateRect(app->hwnd, NULL, FALSE);
            changed = true;
        }
        remote_result_destroy(result);
    }
    return changed;
#else
    (void)app;
    return false;
#endif
}


bool tinta_app_prepare_source(const char *source, size_t length,
                              const wchar_t *path, size_t max_nodes,
                              size_t max_depth, TintaPreparedSource *prepared) {
    TintaStr8 path8 = {0};
    TintaMarkdownOptions markdown_options = tinta_markdown_default_options();
    if (!prepared || (!source && length)) return false;
    memset(prepared, 0, sizeof(*prepared));
#if TINTA_ENABLE_MERMAID
    prepared->focus_mermaid =
        path && tinta_is_mermaid_document_path_utf16(path);
#endif
    if (path && !tinta_utf16_to_utf8(path, wcslen(path), &path8)) return false;
    markdown_options.max_nodes = max_nodes;
    markdown_options.max_depth = max_depth;
    prepared->document = tinta_parse_document(source, length,
        path8.data ? path8.data : "", &markdown_options);
    tinta_str8_destroy(&path8);
    if (!prepared->document.success) {
        tinta_app_destroy_prepared_source(prepared);
        return false;
    }
    {
        TintaVec pending = {0};
        size_t count = 0;
        TintaElement *root = prepared->document.root;
        bool within_limit = tinta_vec_init(&pending, sizeof(TintaElement *)) &&
                            tinta_vec_push(&pending, &root) != NULL;
        while (within_limit && pending.len) {
            TintaElement *element = TINTA_VEC_AT(
                TintaElement *, pending, pending.len - 1);
            size_t child;
            pending.len--;
            count++;
            if (max_nodes && count > max_nodes) {
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
            tinta_app_destroy_prepared_source(prepared);
            return false;
        }
        prepared->ast_nodes = count;
    }
    if (!tinta_str8_assign(&prepared->source, source ? source : "", length)) {
        tinta_app_destroy_prepared_source(prepared);
        return false;
    }
    return true;
}

void tinta_app_destroy_prepared_source(TintaPreparedSource *prepared) {
    if (!prepared) return;
    tinta_parse_result_destroy(&prepared->document);
    tinta_str8_destroy(&prepared->source);
    memset(prepared, 0, sizeof(*prepared));
}

void tinta_app_commit_prepared_source(TintaApp *app,
                                      TintaPreparedSource *prepared,
                                      bool new_document) {
    if (!app || !prepared || !prepared->document.success) return;
#if TINTA_ENABLE_MERMAID
    tinta_app_clear_mermaid_cache(app);
#endif
    tinta_parse_result_destroy(&app->document);
    app->document = prepared->document;
    memset(&prepared->document, 0, sizeof(prepared->document));
    app->document_revision++;
    if (!app->document_revision) app->document_revision = 1;
    app->parse_time_us = app->document.parse_time_us;
    app->ast_node_count = prepared->ast_nodes;
    tinta_str8_destroy(&app->source);
    app->source = prepared->source;
    memset(&prepared->source, 0, sizeof(prepared->source));
    tinta_vec_clear(&app->viewer_search_matches);
    app->viewer_search_index = -1;
    tinta_str16_clear(&app->search_query);
    if (new_document) {
        tinta_app_invalidate_image_requests(app);
        tinta_app_clear_image_resources(app);
    }
    app->layout_dirty = true;
    app->focus_mermaid_on_next_layout = prepared->focus_mermaid;
    if (new_document) {
        app->scroll_y = 0;
        app->scroll_x = 0;
        app->scroll_anchor_pending = false;
        app->selection_anchor = 0;
        app->selection_focus = 0;
    }
    InvalidateRect(app->hwnd, NULL, FALSE);
}

bool tinta_app_update_source(TintaApp *app, const char *source, size_t length,
                             const wchar_t *path, bool new_document) {
    TintaPreparedSource prepared;
    if (!app || !tinta_app_prepare_source(source, length, path,
            app->max_ast_nodes, app->max_ast_depth, &prepared))
        return false;
    tinta_app_commit_prepared_source(app, &prepared, new_document);
    tinta_app_destroy_prepared_source(&prepared);
    return true;
}

bool tinta_app_load_source(TintaApp *app, const char *source, size_t length,
                           const wchar_t *path) {
    return tinta_app_update_source(app, source, length, path, true);
}
