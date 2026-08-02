#include "uia_internal.h"

/* Fragment children for headings and links, including hyperlink invocation. */

static HRESULT STDMETHODCALLTYPE root_fragment_query(
    IRawElementProviderFragment *self, REFIID iid, void **result) {
    return tinta_uia_root_query(root_from_fragment(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_fragment_add_ref(
    IRawElementProviderFragment *self) {
    return tinta_uia_root_add_ref(root_from_fragment(self));
}

static ULONG STDMETHODCALLTYPE root_fragment_release(
    IRawElementProviderFragment *self) {
    return tinta_uia_root_release(root_from_fragment(self));
}

static HRESULT STDMETHODCALLTYPE root_fragment_root_query(
    IRawElementProviderFragmentRoot *self, REFIID iid, void **result) {
    return tinta_uia_root_query(root_from_fragment_root(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_fragment_root_add_ref(
    IRawElementProviderFragmentRoot *self) {
    return tinta_uia_root_add_ref(root_from_fragment_root(self));
}

static ULONG STDMETHODCALLTYPE root_fragment_root_release(
    IRawElementProviderFragmentRoot *self) {
    return tinta_uia_root_release(root_from_fragment_root(self));
}


static size_t semantic_count(TintaUiaRoot *root) {
    size_t count;
    size_t i;
    if (FAILED(tinta_uia_root_available(root, NULL))) return 0;
    count = root->app->headings.len;
    for (i = 0; i < root->app->text_runs.len; i++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun,
                                          root->app->text_runs, i);
        if (run->url && run->url[0]) count++;
    }
    return count;
}

bool tinta_uia_semantic_item(TintaUiaRoot *root, size_t index,
                          TintaSemanticItem *item) {
    TintaApp *app;
    size_t heading_count;
    size_t i;
    if (!item || FAILED(tinta_uia_root_available(root, &app))) return false;
    memset(item, 0, sizeof(*item));
    heading_count = app->headings.len;
    if (index < heading_count) {
        TintaHeading *heading = TINTA_VEC_PTR(TintaHeading, app->headings, index);
        const wchar_t *found = app->doc_text.data && heading->text ?
            wcsstr(app->doc_text.data, heading->text) : NULL;
        item->name = heading->text;
        item->start = found ? (size_t)(found - app->doc_text.data) : 0;
        item->end = item->start + (heading->text ? wcslen(heading->text) : 0);
        item->left = 0;
        item->right = (float)app->width;
        item->top = heading->y - app->scroll_y;
        item->bottom = item->top + 36.0f * app->dpi_scale;
        return true;
    }
    index -= heading_count;
    for (i = 0; i < app->text_runs.len; i++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun, app->text_runs, i);
        if (!run->url || !run->url[0]) continue;
        if (!index) {
            item->link = true;
            item->name = run->text;
            item->url = run->url;
            item->start = run->doc_start;
            item->end = run->doc_start + run->doc_length;
            item->left = run->x - app->scroll_x;
            item->right = item->left + run->width;
            item->top = run->y - app->scroll_y;
            item->bottom = item->top + run->height;
            return true;
        }
        index--;
    }
    return false;
}

static TintaUiaChild *child_create(TintaUiaRoot *root, size_t index) {
    TintaUiaChild *child;
    if (index >= semantic_count(root)) return NULL;
    child = (TintaUiaChild *)calloc(1, sizeof(*child));
    if (!child) return NULL;
    child->simple.lpVtbl = &tinta_uia_child_simple_vtable;
    child->fragment.lpVtbl = &tinta_uia_child_fragment_vtable;
    child->invoke.lpVtbl = &tinta_uia_child_invoke_vtable;
    child->references = 1;
    child->root = root;
    child->index = index;
    tinta_uia_root_add_ref(root);
    return child;
}

static ULONG child_add_ref(TintaUiaChild *child) {
    return (ULONG)InterlockedIncrement(&child->references);
}

static ULONG child_release(TintaUiaChild *child) {
    LONG value = InterlockedDecrement(&child->references);
    if (!value) {
        tinta_uia_root_release(child->root);
        free(child);
    }
    return (ULONG)value;
}

static HRESULT child_query(TintaUiaChild *child, REFIID iid, void **result) {
    TintaSemanticItem item;
    if (!result) return E_POINTER;
    *result = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) ||
        IsEqualIID(iid, &IID_IRawElementProviderSimple))
        *result = &child->simple;
    else if (IsEqualIID(iid, &IID_IRawElementProviderFragment))
        *result = &child->fragment;
    else if (IsEqualIID(iid, &IID_IInvokeProvider) &&
             tinta_uia_semantic_item(child->root, child->index, &item) && item.link)
        *result = &child->invoke;
    else
        return E_NOINTERFACE;
    child_add_ref(child);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_simple_query(
    IRawElementProviderSimple *self, REFIID iid, void **result) {
    return child_query(child_from_simple(self), iid, result);
}

static ULONG STDMETHODCALLTYPE child_simple_add_ref(
    IRawElementProviderSimple *self) {
    return child_add_ref(child_from_simple(self));
}

static ULONG STDMETHODCALLTYPE child_simple_release(
    IRawElementProviderSimple *self) {
    return child_release(child_from_simple(self));
}

static HRESULT STDMETHODCALLTYPE child_fragment_query(
    IRawElementProviderFragment *self, REFIID iid, void **result) {
    return child_query(child_from_fragment(self), iid, result);
}

static ULONG STDMETHODCALLTYPE child_fragment_add_ref(
    IRawElementProviderFragment *self) {
    return child_add_ref(child_from_fragment(self));
}

static ULONG STDMETHODCALLTYPE child_fragment_release(
    IRawElementProviderFragment *self) {
    return child_release(child_from_fragment(self));
}

static HRESULT STDMETHODCALLTYPE child_invoke_query(
    IInvokeProvider *self, REFIID iid, void **result) {
    return child_query(child_from_invoke(self), iid, result);
}

static ULONG STDMETHODCALLTYPE child_invoke_add_ref(IInvokeProvider *self) {
    return child_add_ref(child_from_invoke(self));
}

static ULONG STDMETHODCALLTYPE child_invoke_release(IInvokeProvider *self) {
    return child_release(child_from_invoke(self));
}

static HRESULT STDMETHODCALLTYPE child_get_provider_options(
    IRawElementProviderSimple *self, enum ProviderOptions *result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_get_pattern(
    IRawElementProviderSimple *self, PATTERNID pattern, IUnknown **result) {
    TintaUiaChild *child = child_from_simple(self);
    TintaSemanticItem item;
    if (!result) return E_POINTER;
    *result = NULL;
    if (pattern == UIA_InvokePatternId &&
        tinta_uia_semantic_item(child->root, child->index, &item) && item.link) {
        *result = (IUnknown *)&child->invoke;
        child_add_ref(child);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_get_property(
    IRawElementProviderSimple *self, PROPERTYID property, VARIANT *result) {
    TintaUiaChild *child = child_from_simple(self);
    TintaSemanticItem item;
    if (!result) return E_POINTER;
    VariantInit(result);
    if (!tinta_uia_semantic_item(child->root, child->index, &item))
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (property == UIA_ControlTypePropertyId) {
        V_VT(result) = VT_I4;
        V_I4(result) = item.link ? UIA_HyperlinkControlTypeId :
                                  UIA_HeaderControlTypeId;
    } else if (property == UIA_NamePropertyId) {
        return tinta_uia_variant_bstr(result, item.name);
    } else if (property == UIA_IsControlElementPropertyId ||
               property == UIA_IsContentElementPropertyId ||
               property == UIA_IsEnabledPropertyId) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = VARIANT_TRUE;
    } else if (property == UIA_IsKeyboardFocusablePropertyId) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = VARIANT_FALSE;
    } else if (property == UIA_IsOffscreenPropertyId) {
        TintaApp *app = child->root->app;
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = item.bottom < 0 || item.top > app->height ?
                         VARIANT_TRUE : VARIANT_FALSE;
    }
    return S_OK;
}



static HRESULT STDMETHODCALLTYPE child_get_host(
    IRawElementProviderSimple *self, IRawElementProviderSimple **result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_navigate(
    IRawElementProviderFragment *self, enum NavigateDirection direction,
    IRawElementProviderFragment **result) {
    TintaUiaChild *child = child_from_fragment(self);
    TintaUiaChild *other = NULL;
    size_t count = semantic_count(child->root);
    if (!result) return E_POINTER;
    *result = NULL;
    if (direction == NavigateDirection_Parent) {
        *result = &child->root->fragment;
        tinta_uia_root_add_ref(child->root);
        return S_OK;
    }
    if (direction == NavigateDirection_NextSibling && child->index + 1 < count)
        other = child_create(child->root, child->index + 1);
    else if (direction == NavigateDirection_PreviousSibling && child->index)
        other = child_create(child->root, child->index - 1);
    if (other) *result = &other->fragment;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_runtime_id(
    IRawElementProviderFragment *self, SAFEARRAY **result) {
    TintaUiaChild *child = child_from_fragment(self);
    LONG values[2] = {UiaAppendRuntimeId, (LONG)(child->index + 1)};
    LONG index;
    SAFEARRAY *array;
    if (!result) return E_POINTER;
    *result = NULL;
    array = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!array) return E_OUTOFMEMORY;
    for (index = 0; index < 2; index++)
        SafeArrayPutElement(array, &index, &values[index]);
    *result = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_bounding_rectangle(
    IRawElementProviderFragment *self, struct UiaRect *result) {
    TintaUiaChild *child = child_from_fragment(self);
    TintaSemanticItem item;
    POINT origin = {0, 0};
    if (!result) return E_POINTER;
    if (!tinta_uia_semantic_item(child->root, child->index, &item))
        return UIA_E_ELEMENTNOTAVAILABLE;
    ClientToScreen(child->root->app->hwnd, &origin);
    result->left = origin.x + item.left;
    result->top = origin.y + item.top;
    result->width = item.right - item.left;
    result->height = item.bottom - item.top;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_embedded_roots(
    IRawElementProviderFragment *self, SAFEARRAY **result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_set_focus(
    IRawElementProviderFragment *self) {
    TintaUiaChild *child = child_from_fragment(self);
    if (FAILED(tinta_uia_root_available(child->root, NULL)))
        return UIA_E_ELEMENTNOTAVAILABLE;
    SetFocus(child->root->app->hwnd);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_get_fragment_root(
    IRawElementProviderFragment *self,
    IRawElementProviderFragmentRoot **result) {
    TintaUiaChild *child = child_from_fragment(self);
    if (!result) return E_POINTER;
    *result = &child->root->fragment_root;
    tinta_uia_root_add_ref(child->root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_invoke_action(IInvokeProvider *self) {
    TintaUiaChild *child = child_from_invoke(self);
    TintaSemanticItem item;
    if (!tinta_uia_semantic_item(child->root, child->index, &item) || !item.link)
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (child->root->app->invoke_link)
        child->root->app->invoke_link(child->root->app, item.url);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_fragment_navigate(
    IRawElementProviderFragment *self, enum NavigateDirection direction,
    IRawElementProviderFragment **result) {
    TintaUiaRoot *root = root_from_fragment(self);
    TintaUiaChild *child = NULL;
    size_t count = semantic_count(root);
    if (!result) return E_POINTER;
    *result = NULL;
    if (direction == NavigateDirection_FirstChild && count)
        child = child_create(root, 0);
    else if (direction == NavigateDirection_LastChild && count)
        child = child_create(root, count - 1);
    if (child) *result = &child->fragment;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_fragment_runtime_id(
    IRawElementProviderFragment *self, SAFEARRAY **result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_fragment_bounding_rectangle(
    IRawElementProviderFragment *self, struct UiaRect *result) {
    TintaUiaRoot *root = root_from_fragment(self);
    RECT rect;
    if (!result) return E_POINTER;
    if (FAILED(tinta_uia_root_available(root, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    GetWindowRect(root->app->hwnd, &rect);
    result->left = rect.left;
    result->top = rect.top;
    result->width = rect.right - rect.left;
    result->height = rect.bottom - rect.top;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_fragment_embedded_roots(
    IRawElementProviderFragment *self, SAFEARRAY **result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_fragment_set_focus(
    IRawElementProviderFragment *self) {
    TintaUiaRoot *root = root_from_fragment(self);
    if (FAILED(tinta_uia_root_available(root, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    SetFocus(root->app->hwnd);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_fragment_get_root(
    IRawElementProviderFragment *self,
    IRawElementProviderFragmentRoot **result) {
    TintaUiaRoot *root = root_from_fragment(self);
    if (!result) return E_POINTER;
    *result = &root->fragment_root;
    tinta_uia_root_add_ref(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_element_from_point(
    IRawElementProviderFragmentRoot *self, double x, double y,
    IRawElementProviderFragment **result) {
    TintaUiaRoot *root = root_from_fragment_root(self);
    size_t count = semantic_count(root);
    size_t i;
    POINT origin = {0, 0};
    if (!result) return E_POINTER;
    *result = NULL;
    if (FAILED(tinta_uia_root_available(root, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    ClientToScreen(root->app->hwnd, &origin);
    for (i = 0; i < count; i++) {
        TintaSemanticItem item;
        if (tinta_uia_semantic_item(root, i, &item) &&
            x >= origin.x + item.left && x <= origin.x + item.right &&
            y >= origin.y + item.top && y <= origin.y + item.bottom) {
            TintaUiaChild *child = child_create(root, i);
            if (!child) return E_OUTOFMEMORY;
            *result = &child->fragment;
            break;
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_fragment_get_focus(
    IRawElementProviderFragmentRoot *self,
    IRawElementProviderFragment **result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = NULL;
    return S_OK;
}


IRawElementProviderFragmentVtbl tinta_uia_root_fragment_vtable = {
    root_fragment_query, root_fragment_add_ref, root_fragment_release,
    root_fragment_navigate, root_fragment_runtime_id,
    root_fragment_bounding_rectangle, root_fragment_embedded_roots,
    root_fragment_set_focus, root_fragment_get_root
};

IRawElementProviderFragmentRootVtbl tinta_uia_root_fragment_root_vtable = {
    root_fragment_root_query, root_fragment_root_add_ref,
    root_fragment_root_release, root_element_from_point,
    root_fragment_get_focus
};

IRawElementProviderSimpleVtbl tinta_uia_child_simple_vtable = {
    child_simple_query, child_simple_add_ref, child_simple_release,
    child_get_provider_options, child_get_pattern, child_get_property,
    child_get_host
};

IRawElementProviderFragmentVtbl tinta_uia_child_fragment_vtable = {
    child_fragment_query, child_fragment_add_ref, child_fragment_release,
    child_navigate, child_runtime_id, child_bounding_rectangle,
    child_embedded_roots, child_set_focus, child_get_fragment_root
};

IInvokeProviderVtbl tinta_uia_child_invoke_vtable = {
    child_invoke_query, child_invoke_add_ref, child_invoke_release,
    child_invoke_action
};
