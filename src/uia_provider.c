#include "uia_internal.h"

/* Root provider lifetime, window properties, and the custom scroll pattern. */

HRESULT tinta_uia_root_available(TintaUiaRoot *root, TintaApp **app) {
    if (!root) return UIA_E_ELEMENTNOTAVAILABLE;
    EnterCriticalSection(&root->guard);
    if (!root->app || !IsWindow(root->app->hwnd)) {
        LeaveCriticalSection(&root->guard);
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (app) *app = root->app;
    return S_OK;
}

void tinta_uia_root_done(TintaUiaRoot *root) {
    if (root) LeaveCriticalSection(&root->guard);
}

ULONG tinta_uia_root_add_ref(TintaUiaRoot *root) {
    return (ULONG)InterlockedIncrement(&root->references);
}

ULONG tinta_uia_root_release(TintaUiaRoot *root) {
    LONG value = InterlockedDecrement(&root->references);
    if (!value) {
        DeleteCriticalSection(&root->guard);
        free(root);
    }
    return (ULONG)value;
}

HRESULT tinta_uia_root_query(TintaUiaRoot *root, REFIID iid, void **result) {
    if (!result) return E_POINTER;
    *result = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) ||
        IsEqualIID(iid, &IID_IRawElementProviderSimple))
        *result = &root->simple;
    else if (IsEqualIID(iid, &IID_ITextProvider))
        *result = &root->text;
    else if (IsEqualIID(iid, &IID_IScrollProvider))
        *result = &root->scroll;
    else if (IsEqualIID(iid, &IID_IRawElementProviderFragment))
        *result = &root->fragment;
    else if (IsEqualIID(iid, &IID_IRawElementProviderFragmentRoot))
        *result = &root->fragment_root;
    else
        return E_NOINTERFACE;
    tinta_uia_root_add_ref(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_simple_query(
    IRawElementProviderSimple *self, REFIID iid, void **result) {
    return tinta_uia_root_query(root_from_simple(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_simple_add_ref(
    IRawElementProviderSimple *self) {
    return tinta_uia_root_add_ref(root_from_simple(self));
}

static ULONG STDMETHODCALLTYPE root_simple_release(
    IRawElementProviderSimple *self) {
    return tinta_uia_root_release(root_from_simple(self));
}


static HRESULT STDMETHODCALLTYPE root_scroll_query(
    IScrollProvider *self, REFIID iid, void **result) {
    return tinta_uia_root_query(root_from_scroll(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_scroll_add_ref(IScrollProvider *self) {
    return tinta_uia_root_add_ref(root_from_scroll(self));
}

static ULONG STDMETHODCALLTYPE root_scroll_release(IScrollProvider *self) {
    return tinta_uia_root_release(root_from_scroll(self));
}

static HRESULT STDMETHODCALLTYPE root_get_provider_options(
    IRawElementProviderSimple *self, enum ProviderOptions *result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = ProviderOptions_ServerSideProvider |
              ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_get_pattern(
    IRawElementProviderSimple *self, PATTERNID pattern, IUnknown **result) {
    TintaUiaRoot *root = root_from_simple(self);
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = tinta_uia_root_available(root, NULL);
    if (FAILED(hr)) return hr;
    if (pattern == UIA_TextPatternId) {
        *result = (IUnknown *)&root->text;
        tinta_uia_root_add_ref(root);
    } else if (pattern == UIA_ScrollPatternId) {
        *result = (IUnknown *)&root->scroll;
        tinta_uia_root_add_ref(root);
    }
    tinta_uia_root_done(root);
    return S_OK;
}

HRESULT tinta_uia_variant_bstr(VARIANT *value, const wchar_t *text) {
    V_VT(value) = VT_BSTR;
    V_BSTR(value) = SysAllocString(text ? text : L"");
    return V_BSTR(value) ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE root_get_property(
    IRawElementProviderSimple *self, PROPERTYID property, VARIANT *result) {
    TintaUiaRoot *root = root_from_simple(self);
    TintaApp *app;
    HRESULT hr;
    HRESULT result_hr = S_OK;
    if (!result) return E_POINTER;
    VariantInit(result);
    hr = tinta_uia_root_available(root, &app);
    if (FAILED(hr)) return hr;
    if (property == UIA_ControlTypePropertyId) {
        V_VT(result) = VT_I4;
        V_I4(result) = UIA_DocumentControlTypeId;
    } else if (property == UIA_NamePropertyId) {
        result_hr = tinta_uia_variant_bstr(result, L"Markdown document");
    } else if (property == UIA_AutomationIdPropertyId) {
        result_hr = tinta_uia_variant_bstr(result, L"Tinta.MarkdownView");
    } else if (property == UIA_FrameworkIdPropertyId) {
        result_hr = tinta_uia_variant_bstr(result, L"Tinta");
    } else if (property == UIA_ClassNamePropertyId) {
        result_hr = tinta_uia_variant_bstr(result, L"Tinta.MarkdownView");
    } else if (property == UIA_IsKeyboardFocusablePropertyId ||
               property == UIA_IsControlElementPropertyId ||
               property == UIA_IsContentElementPropertyId ||
               property == UIA_IsEnabledPropertyId) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = VARIANT_TRUE;
    } else if (property == UIA_HasKeyboardFocusPropertyId) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = GetFocus() == app->hwnd ? VARIANT_TRUE : VARIANT_FALSE;
    } else if (property == UIA_NativeWindowHandlePropertyId) {
        V_VT(result) = VT_I4;
        V_I4(result) = (LONG)(LONG_PTR)app->hwnd;
    }
    tinta_uia_root_done(root);
    return result_hr;
}

static HRESULT STDMETHODCALLTYPE root_get_host(
    IRawElementProviderSimple *self, IRawElementProviderSimple **result) {
    TintaUiaRoot *root = root_from_simple(self);
    TintaApp *app;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = tinta_uia_root_available(root, &app);
    if (FAILED(hr)) return hr;
    hr = UiaHostProviderFromHwnd(app->hwnd, result);
    tinta_uia_root_done(root);
    return hr;
}


static double scroll_percent(float value, float content, float viewport) {
    float maximum = content - viewport;
    return maximum > 0 ? value / maximum * 100.0 : UIA_ScrollPatternNoScroll;
}

static HRESULT STDMETHODCALLTYPE scroll_action(IScrollProvider *self,
    enum ScrollAmount horizontal, enum ScrollAmount vertical) {
    TintaUiaRoot *root = root_from_scroll(self);
    TintaApp *app;
    float dx = 0;
    float dy = 0;
    HRESULT hr = tinta_uia_root_available(root, &app);
    if (FAILED(hr)) return hr;
    if (horizontal == ScrollAmount_LargeDecrement) dx = -app->width * 0.8f;
    else if (horizontal == ScrollAmount_SmallDecrement) dx = -42.0f;
    else if (horizontal == ScrollAmount_LargeIncrement) dx = app->width * 0.8f;
    else if (horizontal == ScrollAmount_SmallIncrement) dx = 42.0f;
    if (vertical == ScrollAmount_LargeDecrement) dy = -app->height * 0.8f;
    else if (vertical == ScrollAmount_SmallDecrement) dy = -42.0f;
    else if (vertical == ScrollAmount_LargeIncrement) dy = app->height * 0.8f;
    else if (vertical == ScrollAmount_SmallIncrement) dy = 42.0f;
    app->scroll_x = max(0.0f, min(app->scroll_x + dx,
        max(0.0f, app->content_width - app->width)));
    app->scroll_y = max(0.0f, min(app->scroll_y + dy,
        max(0.0f, app->content_height - app->height)));
    InvalidateRect(app->hwnd, NULL, FALSE);
    tinta_uia_root_done(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_set_percent(IScrollProvider *self,
    double horizontal, double vertical) {
    TintaUiaRoot *root = root_from_scroll(self);
    TintaApp *app;
    HRESULT hr = tinta_uia_root_available(root, &app);
    if (FAILED(hr)) return hr;
    if (horizontal != UIA_ScrollPatternNoScroll)
        app->scroll_x = (float)(max(0.0, min(horizontal, 100.0)) / 100.0 *
            max(0.0f, app->content_width - app->width));
    if (vertical != UIA_ScrollPatternNoScroll)
        app->scroll_y = (float)(max(0.0, min(vertical, 100.0)) / 100.0 *
            max(0.0f, app->content_height - app->height));
    InvalidateRect(app->hwnd, NULL, FALSE);
    tinta_uia_root_done(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_horizontal_percent(
    IScrollProvider *self, double *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    if (!result) return E_POINTER;
    HRESULT hr = tinta_uia_root_available(root, &app); if (FAILED(hr)) return hr;
    *result = scroll_percent(app->scroll_x, app->content_width, (float)app->width);
    tinta_uia_root_done(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_vertical_percent(
    IScrollProvider *self, double *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    if (!result) return E_POINTER;
    HRESULT hr = tinta_uia_root_available(root, &app); if (FAILED(hr)) return hr;
    *result = scroll_percent(app->scroll_y, app->content_height, (float)app->height);
    tinta_uia_root_done(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_horizontal_view(
    IScrollProvider *self, double *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    if (!result) return E_POINTER;
    HRESULT hr = tinta_uia_root_available(root, &app); if (FAILED(hr)) return hr;
    *result = app->content_width > 0 ? min(100.0,
        app->width / app->content_width * 100.0) : 100.0;
    tinta_uia_root_done(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_vertical_view(
    IScrollProvider *self, double *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    if (!result) return E_POINTER;
    HRESULT hr = tinta_uia_root_available(root, &app); if (FAILED(hr)) return hr;
    *result = app->content_height > 0 ? min(100.0,
        app->height / app->content_height * 100.0) : 100.0;
    tinta_uia_root_done(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_horizontal_scrollable(
    IScrollProvider *self, BOOL *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    if (!result) return E_POINTER;
    HRESULT hr = tinta_uia_root_available(root, &app); if (FAILED(hr)) return hr;
    *result = app->content_width > app->width;
    tinta_uia_root_done(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_vertical_scrollable(
    IScrollProvider *self, BOOL *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    if (!result) return E_POINTER;
    HRESULT hr = tinta_uia_root_available(root, &app); if (FAILED(hr)) return hr;
    *result = app->content_height > app->height;
    tinta_uia_root_done(root);
    return S_OK;
}

static IRawElementProviderSimpleVtbl root_simple_vtable = {
    root_simple_query, root_simple_add_ref, root_simple_release,
    root_get_provider_options, root_get_pattern, root_get_property, root_get_host
};

static IScrollProviderVtbl root_scroll_vtable = {
    root_scroll_query, root_scroll_add_ref, root_scroll_release,
    scroll_action, scroll_set_percent, scroll_get_horizontal_percent,
    scroll_get_vertical_percent, scroll_get_horizontal_view,
    scroll_get_vertical_view, scroll_get_horizontal_scrollable,
    scroll_get_vertical_scrollable
};

static TintaUiaRoot *root_create(TintaApp *app) {
    TintaUiaRoot *root = (TintaUiaRoot *)calloc(1, sizeof(*root));
    if (!root) return NULL;
    root->simple.lpVtbl = &root_simple_vtable;
    root->fragment.lpVtbl = &tinta_uia_root_fragment_vtable;
    root->fragment_root.lpVtbl = &tinta_uia_root_fragment_root_vtable;
    root->text.lpVtbl = &tinta_uia_text_vtable;
    root->scroll.lpVtbl = &root_scroll_vtable;
    root->references = 1;
    InitializeCriticalSection(&root->guard);
    root->app = app;
    return root;
}

LRESULT tinta_uia_get_object(TintaApp *app, WPARAM wparam, LPARAM lparam) {
    TintaUiaRoot *root;
    if (!app || lparam != UiaRootObjectId) return 0;
    root = (TintaUiaRoot *)app->uia_provider;
    if (!root) {
        root = root_create(app);
        if (!root) return 0;
        app->uia_provider = root;
    }
    return UiaReturnRawElementProvider(app->hwnd, wparam, lparam,
                                       &root->simple);
}

void tinta_uia_disconnect(TintaApp *app) {
    TintaUiaRoot *root;
    if (!app || !app->uia_provider) return;
    root = (TintaUiaRoot *)app->uia_provider;
    app->uia_provider = NULL;
    root->app = NULL;
    LeaveCriticalSection(&root->guard);
    UiaDisconnectProvider(&root->simple);
    EnterCriticalSection(&root->guard);
    tinta_uia_root_release(root);
}

typedef struct TintaUiaEventWork {
    TintaUiaRoot *root;
    EVENTID event_id;
} TintaUiaEventWork;

static DWORD CALLBACK raise_event_worker(void *parameter) {
    TintaUiaEventWork *work = (TintaUiaEventWork *)parameter;
    UiaRaiseAutomationEvent(&work->root->simple, work->event_id);
    tinta_uia_root_release(work->root);
    free(work);
    return 0;
}

static void queue_event(TintaUiaRoot *root, EVENTID event_id) {
    TintaUiaEventWork *work;
    if (!root) return;
    work = (TintaUiaEventWork *)malloc(sizeof(*work));
    if (!work) return;
    work->root = root;
    work->event_id = event_id;
    tinta_uia_root_add_ref(root);
    if (!QueueUserWorkItem(raise_event_worker, work, WT_EXECUTEDEFAULT)) {
        tinta_uia_root_release(root);
        free(work);
    }
}

void *tinta_uia_lock_app(TintaApp *app) {
    TintaUiaRoot *root;
    if (!app || !(root = (TintaUiaRoot *)app->uia_provider)) return NULL;
    tinta_uia_root_add_ref(root);
    EnterCriticalSection(&root->guard);
    return root;
}

void tinta_uia_unlock_app(void *lock) {
    TintaUiaRoot *root = (TintaUiaRoot *)lock;
    if (!root) return;
    LeaveCriticalSection(&root->guard);
    tinta_uia_root_release(root);
}

void tinta_uia_raise_text_changed(TintaApp *app) {
    TintaUiaRoot *root = app ? (TintaUiaRoot *)app->uia_provider : NULL;
    queue_event(root, UIA_Text_TextChangedEventId);
}

void tinta_uia_raise_selection_changed(TintaApp *app) {
    TintaUiaRoot *root = app ? (TintaUiaRoot *)app->uia_provider : NULL;
    queue_event(root, UIA_Text_TextSelectionChangedEventId);
}
