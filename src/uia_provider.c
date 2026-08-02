#include "uia_provider.h"

#include <UIAutomationCore.h>
#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>
#include <oleauto.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

typedef struct TintaUiaRoot TintaUiaRoot;

typedef struct TintaUiaChild {
    IRawElementProviderSimple simple;
    IRawElementProviderFragment fragment;
    IInvokeProvider invoke;
    LONG references;
    TintaUiaRoot *root;
    size_t index;
} TintaUiaChild;

typedef struct TintaUiaRange {
    ITextRangeProvider iface;
    LONG references;
    TintaUiaRoot *root;
    size_t start;
    size_t end;
} TintaUiaRange;

struct TintaUiaRoot {
    IRawElementProviderSimple simple;
    IRawElementProviderFragment fragment;
    IRawElementProviderFragmentRoot fragment_root;
    ITextProvider text;
    IScrollProvider scroll;
    LONG references;
    TintaApp *app;
};

static IRawElementProviderSimpleVtbl root_simple_vtable;
static IRawElementProviderFragmentVtbl root_fragment_vtable;
static IRawElementProviderFragmentRootVtbl root_fragment_root_vtable;
static ITextProviderVtbl root_text_vtable;
static IScrollProviderVtbl root_scroll_vtable;
static ITextRangeProviderVtbl range_vtable;
static IRawElementProviderSimpleVtbl child_simple_vtable;
static IRawElementProviderFragmentVtbl child_fragment_vtable;
static IInvokeProviderVtbl child_invoke_vtable;

static TintaUiaRoot *root_from_simple(IRawElementProviderSimple *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, simple);
}

static TintaUiaRoot *root_from_text(ITextProvider *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, text);
}

static TintaUiaRoot *root_from_fragment(IRawElementProviderFragment *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, fragment);
}

static TintaUiaRoot *root_from_fragment_root(
    IRawElementProviderFragmentRoot *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, fragment_root);
}

static TintaUiaRoot *root_from_scroll(IScrollProvider *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, scroll);
}

static TintaUiaRange *range_from_interface(ITextRangeProvider *value) {
    return CONTAINING_RECORD(value, TintaUiaRange, iface);
}

static TintaUiaChild *child_from_simple(IRawElementProviderSimple *value) {
    return CONTAINING_RECORD(value, TintaUiaChild, simple);
}

static TintaUiaChild *child_from_fragment(IRawElementProviderFragment *value) {
    return CONTAINING_RECORD(value, TintaUiaChild, fragment);
}

static TintaUiaChild *child_from_invoke(IInvokeProvider *value) {
    return CONTAINING_RECORD(value, TintaUiaChild, invoke);
}

static HRESULT root_available(TintaUiaRoot *root, TintaApp **app) {
    if (!root || !root->app || !IsWindow(root->app->hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (app) *app = root->app;
    return S_OK;
}

static ULONG root_add_ref(TintaUiaRoot *root) {
    return (ULONG)InterlockedIncrement(&root->references);
}

static ULONG root_release(TintaUiaRoot *root) {
    LONG value = InterlockedDecrement(&root->references);
    if (!value) free(root);
    return (ULONG)value;
}

static HRESULT root_query(TintaUiaRoot *root, REFIID iid, void **result) {
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
    root_add_ref(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_simple_query(
    IRawElementProviderSimple *self, REFIID iid, void **result) {
    return root_query(root_from_simple(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_simple_add_ref(
    IRawElementProviderSimple *self) {
    return root_add_ref(root_from_simple(self));
}

static ULONG STDMETHODCALLTYPE root_simple_release(
    IRawElementProviderSimple *self) {
    return root_release(root_from_simple(self));
}

static HRESULT STDMETHODCALLTYPE root_fragment_query(
    IRawElementProviderFragment *self, REFIID iid, void **result) {
    return root_query(root_from_fragment(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_fragment_add_ref(
    IRawElementProviderFragment *self) {
    return root_add_ref(root_from_fragment(self));
}

static ULONG STDMETHODCALLTYPE root_fragment_release(
    IRawElementProviderFragment *self) {
    return root_release(root_from_fragment(self));
}

static HRESULT STDMETHODCALLTYPE root_fragment_root_query(
    IRawElementProviderFragmentRoot *self, REFIID iid, void **result) {
    return root_query(root_from_fragment_root(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_fragment_root_add_ref(
    IRawElementProviderFragmentRoot *self) {
    return root_add_ref(root_from_fragment_root(self));
}

static ULONG STDMETHODCALLTYPE root_fragment_root_release(
    IRawElementProviderFragmentRoot *self) {
    return root_release(root_from_fragment_root(self));
}

static HRESULT STDMETHODCALLTYPE root_text_query(
    ITextProvider *self, REFIID iid, void **result) {
    return root_query(root_from_text(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_text_add_ref(ITextProvider *self) {
    return root_add_ref(root_from_text(self));
}

static ULONG STDMETHODCALLTYPE root_text_release(ITextProvider *self) {
    return root_release(root_from_text(self));
}

static HRESULT STDMETHODCALLTYPE root_scroll_query(
    IScrollProvider *self, REFIID iid, void **result) {
    return root_query(root_from_scroll(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_scroll_add_ref(IScrollProvider *self) {
    return root_add_ref(root_from_scroll(self));
}

static ULONG STDMETHODCALLTYPE root_scroll_release(IScrollProvider *self) {
    return root_release(root_from_scroll(self));
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
    if (!result) return E_POINTER;
    *result = NULL;
    if (pattern == UIA_TextPatternId) {
        *result = (IUnknown *)&root->text;
        root_add_ref(root);
    } else if (pattern == UIA_ScrollPatternId) {
        *result = (IUnknown *)&root->scroll;
        root_add_ref(root);
    }
    return S_OK;
}

static HRESULT variant_bstr(VARIANT *value, const wchar_t *text) {
    V_VT(value) = VT_BSTR;
    V_BSTR(value) = SysAllocString(text ? text : L"");
    return V_BSTR(value) ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE root_get_property(
    IRawElementProviderSimple *self, PROPERTYID property, VARIANT *result) {
    TintaUiaRoot *root = root_from_simple(self);
    TintaApp *app;
    HRESULT hr;
    if (!result) return E_POINTER;
    VariantInit(result);
    hr = root_available(root, &app);
    if (FAILED(hr)) return hr;
    if (property == UIA_ControlTypePropertyId) {
        V_VT(result) = VT_I4;
        V_I4(result) = UIA_DocumentControlTypeId;
    } else if (property == UIA_NamePropertyId) {
        return variant_bstr(result, L"Markdown document");
    } else if (property == UIA_AutomationIdPropertyId) {
        return variant_bstr(result, L"Tinta.MarkdownView");
    } else if (property == UIA_FrameworkIdPropertyId) {
        return variant_bstr(result, L"Tinta");
    } else if (property == UIA_ClassNamePropertyId) {
        return variant_bstr(result, L"Tinta.MarkdownView");
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
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_get_host(
    IRawElementProviderSimple *self, IRawElementProviderSimple **result) {
    TintaUiaRoot *root = root_from_simple(self);
    TintaApp *app;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = root_available(root, &app);
    if (FAILED(hr)) return hr;
    return UiaHostProviderFromHwnd(app->hwnd, result);
}

typedef struct TintaSemanticItem {
    bool link;
    const wchar_t *name;
    const char *url;
    size_t start;
    size_t end;
    float left;
    float top;
    float right;
    float bottom;
} TintaSemanticItem;

static size_t semantic_count(TintaUiaRoot *root) {
    size_t count;
    size_t i;
    if (FAILED(root_available(root, NULL))) return 0;
    count = root->app->headings.len;
    for (i = 0; i < root->app->text_runs.len; i++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun,
                                          root->app->text_runs, i);
        if (run->url && run->url[0]) count++;
    }
    return count;
}

static bool semantic_item(TintaUiaRoot *root, size_t index,
                          TintaSemanticItem *item) {
    TintaApp *app;
    size_t heading_count;
    size_t i;
    if (!item || FAILED(root_available(root, &app))) return false;
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
    child->simple.lpVtbl = &child_simple_vtable;
    child->fragment.lpVtbl = &child_fragment_vtable;
    child->invoke.lpVtbl = &child_invoke_vtable;
    child->references = 1;
    child->root = root;
    child->index = index;
    root_add_ref(root);
    return child;
}

static ULONG child_add_ref(TintaUiaChild *child) {
    return (ULONG)InterlockedIncrement(&child->references);
}

static ULONG child_release(TintaUiaChild *child) {
    LONG value = InterlockedDecrement(&child->references);
    if (!value) {
        root_release(child->root);
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
             semantic_item(child->root, child->index, &item) && item.link)
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
        semantic_item(child->root, child->index, &item) && item.link) {
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
    if (!semantic_item(child->root, child->index, &item))
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (property == UIA_ControlTypePropertyId) {
        V_VT(result) = VT_I4;
        V_I4(result) = item.link ? UIA_HyperlinkControlTypeId :
                                  UIA_HeaderControlTypeId;
    } else if (property == UIA_NamePropertyId) {
        return variant_bstr(result, item.name);
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
        root_add_ref(child->root);
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
    if (!semantic_item(child->root, child->index, &item))
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
    if (FAILED(root_available(child->root, NULL)))
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
    root_add_ref(child->root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE child_invoke_action(IInvokeProvider *self) {
    TintaUiaChild *child = child_from_invoke(self);
    TintaSemanticItem item;
    if (!semantic_item(child->root, child->index, &item) || !item.link)
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
    if (FAILED(root_available(root, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
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
    if (FAILED(root_available(root, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    SetFocus(root->app->hwnd);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_fragment_get_root(
    IRawElementProviderFragment *self,
    IRawElementProviderFragmentRoot **result) {
    TintaUiaRoot *root = root_from_fragment(self);
    if (!result) return E_POINTER;
    *result = &root->fragment_root;
    root_add_ref(root);
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
    if (FAILED(root_available(root, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    ClientToScreen(root->app->hwnd, &origin);
    for (i = 0; i < count; i++) {
        TintaSemanticItem item;
        if (semantic_item(root, i, &item) &&
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

static TintaUiaRange *range_create(TintaUiaRoot *root,
                                   size_t start, size_t end) {
    TintaUiaRange *range;
    size_t length;
    if (FAILED(root_available(root, NULL))) return NULL;
    length = root->app->doc_text.len;
    if (start > length) start = length;
    if (end > length) end = length;
    if (start > end) start = end;
    range = (TintaUiaRange *)calloc(1, sizeof(*range));
    if (!range) return NULL;
    range->iface.lpVtbl = &range_vtable;
    range->references = 1;
    range->root = root;
    range->start = start;
    range->end = end;
    root_add_ref(root);
    return range;
}

static HRESULT range_array(TintaUiaRange *range, SAFEARRAY **result) {
    LONG index = 0;
    IUnknown *unknown;
    SAFEARRAY *array;
    if (!result) return E_POINTER;
    *result = NULL;
    array = SafeArrayCreateVector(VT_UNKNOWN, 0, range ? 1 : 0);
    if (!array) return E_OUTOFMEMORY;
    if (range) {
        unknown = (IUnknown *)&range->iface;
        if (FAILED(SafeArrayPutElement(array, &index, unknown))) {
            SafeArrayDestroy(array);
            return E_FAIL;
        }
    }
    *result = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_get_selection(ITextProvider *self,
                                                     SAFEARRAY **result) {
    TintaUiaRoot *root = root_from_text(self);
    TintaApp *app;
    TintaUiaRange *range;
    HRESULT hr = root_available(root, &app);
    if (FAILED(hr)) return hr;
    range = range_create(root,
        min(app->selection_anchor, app->selection_focus),
        max(app->selection_anchor, app->selection_focus));
    if (!range) return E_OUTOFMEMORY;
    hr = range_array(range, result);
    ITextRangeProvider_Release(&range->iface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_get_visible_ranges(ITextProvider *self,
                                                          SAFEARRAY **result) {
    TintaUiaRoot *root = root_from_text(self);
    TintaApp *app;
    TintaUiaRange *range;
    size_t start = 0;
    size_t end;
    size_t i;
    bool found = false;
    HRESULT hr = root_available(root, &app);
    if (FAILED(hr)) return hr;
    end = app->doc_text.len;
    for (i = 0; i < app->text_runs.len; i++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun, app->text_runs, i);
        if (run->y + run->height >= app->scroll_y &&
            run->y <= app->scroll_y + app->height) {
            if (!found) start = run->doc_start;
            end = run->doc_start + run->doc_length;
            found = true;
        }
    }
    range = range_create(root, start, end);
    if (!range) return E_OUTOFMEMORY;
    hr = range_array(range, result);
    ITextRangeProvider_Release(&range->iface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_range_from_child(ITextProvider *self,
    IRawElementProviderSimple *child, ITextRangeProvider **result) {
    TintaUiaRoot *root = root_from_text(self);
    TintaUiaChild *semantic;
    TintaSemanticItem item;
    TintaUiaRange *range;
    if (!result) return E_POINTER;
    *result = NULL;
    if (!child || child->lpVtbl != &child_simple_vtable) return E_INVALIDARG;
    semantic = child_from_simple(child);
    if (semantic->root != root ||
        !semantic_item(root, semantic->index, &item)) return E_INVALIDARG;
    range = range_create(root, item.start, item.end);
    if (!range) return E_OUTOFMEMORY;
    *result = &range->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_range_from_point(ITextProvider *self,
    struct UiaPoint point, ITextRangeProvider **result) {
    TintaUiaRoot *root = root_from_text(self);
    TintaApp *app;
    POINT client;
    size_t position = 0;
    TintaUiaRange *range;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = root_available(root, &app);
    if (FAILED(hr)) return hr;
    client.x = (LONG)point.x;
    client.y = (LONG)point.y;
    ScreenToClient(app->hwnd, &client);
    tinta_hit_test(app, (float)client.x, (float)client.y, &position, NULL);
    range = range_create(root, position, position);
    if (!range) return E_OUTOFMEMORY;
    *result = &range->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_get_document_range(ITextProvider *self,
    ITextRangeProvider **result) {
    TintaUiaRoot *root = root_from_text(self);
    TintaUiaRange *range;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = root_available(root, NULL);
    if (FAILED(hr)) return hr;
    range = range_create(root, 0, root->app->doc_text.len);
    if (!range) return E_OUTOFMEMORY;
    *result = &range->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_get_selection_mode(ITextProvider *self,
    enum SupportedTextSelection *result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = SupportedTextSelection_Single;
    return S_OK;
}

static HRESULT range_query(TintaUiaRange *range, REFIID iid, void **result) {
    if (!result) return E_POINTER;
    *result = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) &&
        !IsEqualIID(iid, &IID_ITextRangeProvider)) return E_NOINTERFACE;
    *result = &range->iface;
    InterlockedIncrement(&range->references);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_query_interface(ITextRangeProvider *self,
    REFIID iid, void **result) {
    return range_query(range_from_interface(self), iid, result);
}

static ULONG STDMETHODCALLTYPE range_add_ref(ITextRangeProvider *self) {
    return (ULONG)InterlockedIncrement(&range_from_interface(self)->references);
}

static ULONG STDMETHODCALLTYPE range_release(ITextRangeProvider *self) {
    TintaUiaRange *range = range_from_interface(self);
    LONG value = InterlockedDecrement(&range->references);
    if (!value) {
        root_release(range->root);
        free(range);
    }
    return (ULONG)value;
}

static HRESULT range_valid(TintaUiaRange *range, TintaApp **app) {
    return range ? root_available(range->root, app) : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE range_clone(ITextRangeProvider *self,
                                              ITextRangeProvider **result) {
    TintaUiaRange *range = range_from_interface(self);
    TintaUiaRange *copy;
    if (!result) return E_POINTER;
    *result = NULL;
    if (FAILED(range_valid(range, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    copy = range_create(range->root, range->start, range->end);
    if (!copy) return E_OUTOFMEMORY;
    *result = &copy->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_compare(ITextRangeProvider *self,
    ITextRangeProvider *other, BOOL *result) {
    TintaUiaRange *left = range_from_interface(self);
    TintaUiaRange *right;
    if (!other || !result) return E_INVALIDARG;
    right = range_from_interface(other);
    *result = left->root == right->root && left->start == right->start &&
              left->end == right->end;
    return S_OK;
}

static size_t range_endpoint(TintaUiaRange *range,
                             enum TextPatternRangeEndpoint endpoint) {
    return endpoint == TextPatternRangeEndpoint_Start ? range->start : range->end;
}

static HRESULT STDMETHODCALLTYPE range_compare_endpoints(ITextRangeProvider *self,
    enum TextPatternRangeEndpoint endpoint, ITextRangeProvider *other,
    enum TextPatternRangeEndpoint other_endpoint, int *result) {
    size_t left;
    size_t right;
    if (!other || !result) return E_INVALIDARG;
    left = range_endpoint(range_from_interface(self), endpoint);
    right = range_endpoint(range_from_interface(other), other_endpoint);
    *result = left < right ? -1 : left > right ? 1 : 0;
    return S_OK;
}

static void expand_word(const wchar_t *text, size_t length,
                        size_t *start, size_t *end) {
    while (*start && (iswalnum(text[*start - 1]) || text[*start - 1] == L'_'))
        (*start)--;
    while (*end < length && (iswalnum(text[*end]) || text[*end] == L'_'))
        (*end)++;
}

static void expand_line(const wchar_t *text, size_t length,
                        size_t *start, size_t *end) {
    while (*start && text[*start - 1] != L'\n') (*start)--;
    while (*end < length && text[*end] != L'\n') (*end)++;
    if (*end < length) (*end)++;
}

static HRESULT STDMETHODCALLTYPE range_expand(ITextRangeProvider *self,
                                               enum TextUnit unit) {
    TintaUiaRange *range = range_from_interface(self);
    TintaApp *app;
    HRESULT hr = range_valid(range, &app);
    if (FAILED(hr)) return hr;
    if (unit == TextUnit_Document) {
        range->start = 0;
        range->end = app->doc_text.len;
    } else if (unit == TextUnit_Character) {
        if (range->start < app->doc_text.len) range->end = range->start + 1;
        else range->end = range->start;
    } else if (unit == TextUnit_Word) {
        range->end = range->start;
        expand_word(app->doc_text.data, app->doc_text.len,
                    &range->start, &range->end);
    } else {
        range->end = range->start;
        expand_line(app->doc_text.data, app->doc_text.len,
                    &range->start, &range->end);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_find_attribute(ITextRangeProvider *self,
    TEXTATTRIBUTEID attribute, VARIANT value, BOOL backward,
    ITextRangeProvider **result) {
    (void)self; (void)attribute; (void)value; (void)backward;
    if (!result) return E_POINTER;
    *result = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_find_text(ITextRangeProvider *self,
    BSTR text, BOOL backward, BOOL ignore_case, ITextRangeProvider **result) {
    TintaUiaRange *range = range_from_interface(self);
    TintaApp *app;
    size_t query_length;
    size_t position;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = range_valid(range, &app);
    if (FAILED(hr)) return hr;
    if (!text || !(query_length = SysStringLen(text)) ||
        query_length > range->end - range->start) return S_OK;
    if (backward) {
        position = range->end - query_length;
        for (;;) {
            if ((ignore_case ? _wcsnicmp(app->doc_text.data + position, text,
                                         query_length) :
                               wcsncmp(app->doc_text.data + position, text,
                                       query_length)) == 0) break;
            if (position == range->start) return S_OK;
            position--;
        }
    } else {
        for (position = range->start;
             position + query_length <= range->end; position++) {
            if ((ignore_case ? _wcsnicmp(app->doc_text.data + position, text,
                                         query_length) :
                               wcsncmp(app->doc_text.data + position, text,
                                       query_length)) == 0) break;
        }
        if (position + query_length > range->end) return S_OK;
    }
    {
        TintaUiaRange *found = range_create(range->root, position,
                                            position + query_length);
        if (!found) return E_OUTOFMEMORY;
        *result = &found->iface;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_get_attribute(ITextRangeProvider *self,
    TEXTATTRIBUTEID attribute, VARIANT *result) {
    IUnknown *not_supported = NULL;
    (void)self;
    (void)attribute;
    if (!result) return E_POINTER;
    VariantInit(result);
    if (SUCCEEDED(UiaGetReservedNotSupportedValue(&not_supported))) {
        V_VT(result) = VT_UNKNOWN;
        V_UNKNOWN(result) = not_supported;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_get_rectangles(ITextRangeProvider *self,
                                                       SAFEARRAY **result) {
    TintaUiaRange *range = range_from_interface(self);
    TintaApp *app;
    SAFEARRAY *array;
    RECT rect;
    LONG index;
    double values[4];
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = range_valid(range, &app);
    if (FAILED(hr)) return hr;
    array = SafeArrayCreateVector(VT_R8, 0, range->start == range->end ? 0 : 4);
    if (!array) return E_OUTOFMEMORY;
    if (range->start != range->end) {
        GetWindowRect(app->hwnd, &rect);
        values[0] = rect.left;
        values[1] = rect.top;
        values[2] = rect.right - rect.left;
        values[3] = rect.bottom - rect.top;
        for (index = 0; index < 4; index++)
            SafeArrayPutElement(array, &index, &values[index]);
    }
    *result = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_get_enclosing(ITextRangeProvider *self,
    IRawElementProviderSimple **result) {
    TintaUiaRange *range = range_from_interface(self);
    if (!result) return E_POINTER;
    if (FAILED(range_valid(range, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    *result = &range->root->simple;
    root_add_ref(range->root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_get_text(ITextRangeProvider *self,
    int maximum, BSTR *result) {
    TintaUiaRange *range = range_from_interface(self);
    TintaApp *app;
    size_t length;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = range_valid(range, &app);
    if (FAILED(hr)) return hr;
    length = range->end - range->start;
    if (maximum >= 0 && length > (size_t)maximum) length = (size_t)maximum;
    *result = SysAllocStringLen(app->doc_text.data + range->start, (UINT)length);
    return *result || !length ? S_OK : E_OUTOFMEMORY;
}

static size_t move_position(const wchar_t *text, size_t length,
                            size_t position, enum TextUnit unit, int direction) {
    if (unit == TextUnit_Document) return direction < 0 ? 0 : length;
    if (unit == TextUnit_Character) {
        if (direction < 0 && position) return position - 1;
        if (direction > 0 && position < length) return position + 1;
        return position;
    }
    if (direction < 0) {
        if (position) position--;
        while (position && text[position - 1] != L'\n' &&
               (unit != TextUnit_Word || iswalnum(text[position - 1])))
            position--;
    } else {
        while (position < length && text[position] != L'\n' &&
               (unit != TextUnit_Word || iswalnum(text[position])))
            position++;
        if (position < length) position++;
    }
    return position;
}

static HRESULT STDMETHODCALLTYPE range_move(ITextRangeProvider *self,
    enum TextUnit unit, int count, int *moved) {
    TintaUiaRange *range = range_from_interface(self);
    TintaApp *app;
    size_t position;
    int actual = 0;
    int direction = count < 0 ? -1 : 1;
    HRESULT hr;
    if (!moved) return E_POINTER;
    *moved = 0;
    hr = range_valid(range, &app);
    if (FAILED(hr)) return hr;
    position = range->start;
    while (actual != count) {
        size_t next = move_position(app->doc_text.data, app->doc_text.len,
                                    position, unit, direction);
        if (next == position) break;
        position = next;
        actual += direction;
    }
    range->start = range->end = position;
    *moved = actual;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_move_endpoint(ITextRangeProvider *self,
    enum TextPatternRangeEndpoint endpoint, enum TextUnit unit, int count,
    int *moved) {
    TintaUiaRange *range = range_from_interface(self);
    TintaApp *app;
    size_t position;
    int actual = 0;
    int direction = count < 0 ? -1 : 1;
    HRESULT hr;
    if (!moved) return E_POINTER;
    hr = range_valid(range, &app);
    if (FAILED(hr)) return hr;
    position = range_endpoint(range, endpoint);
    while (actual != count) {
        size_t next = move_position(app->doc_text.data, app->doc_text.len,
                                    position, unit, direction);
        if (next == position) break;
        position = next;
        actual += direction;
    }
    if (endpoint == TextPatternRangeEndpoint_Start) {
        range->start = position;
        if (range->start > range->end) range->end = range->start;
    } else {
        range->end = position;
        if (range->end < range->start) range->start = range->end;
    }
    *moved = actual;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_move_endpoint_range(
    ITextRangeProvider *self, enum TextPatternRangeEndpoint endpoint,
    ITextRangeProvider *other,
    enum TextPatternRangeEndpoint other_endpoint) {
    TintaUiaRange *range = range_from_interface(self);
    TintaUiaRange *target;
    size_t position;
    if (!other) return E_INVALIDARG;
    target = range_from_interface(other);
    if (range->root != target->root) return E_INVALIDARG;
    position = range_endpoint(target, other_endpoint);
    if (endpoint == TextPatternRangeEndpoint_Start) {
        range->start = position;
        if (range->start > range->end) range->end = range->start;
    } else {
        range->end = position;
        if (range->end < range->start) range->start = range->end;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_select(ITextRangeProvider *self) {
    TintaUiaRange *range = range_from_interface(self);
    TintaApp *app;
    HRESULT hr = range_valid(range, &app);
    if (FAILED(hr)) return hr;
    app->selection_anchor = range->start;
    app->selection_focus = range->end;
    InvalidateRect(app->hwnd, NULL, FALSE);
    UiaRaiseAutomationEvent(&range->root->simple,
                            UIA_Text_TextSelectionChangedEventId);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_add_selection(ITextRangeProvider *self) {
    (void)self;
    return UIA_E_INVALIDOPERATION;
}

static HRESULT STDMETHODCALLTYPE range_remove_selection(ITextRangeProvider *self) {
    (void)self;
    return UIA_E_INVALIDOPERATION;
}

static HRESULT STDMETHODCALLTYPE range_scroll_into_view(ITextRangeProvider *self,
                                                         BOOL align_top) {
    TintaUiaRange *range = range_from_interface(self);
    TintaApp *app;
    size_t i;
    HRESULT hr = range_valid(range, &app);
    if (FAILED(hr)) return hr;
    for (i = 0; i < app->text_runs.len; i++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun, app->text_runs, i);
        if (range->start <= run->doc_start + run->doc_length &&
            range->end >= run->doc_start) {
            float target = align_top ? run->y : run->y - app->height + run->height;
            app->scroll_y = max(0.0f, min(target,
                max(0.0f, app->content_height - app->height)));
            InvalidateRect(app->hwnd, NULL, FALSE);
            break;
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_get_children(ITextRangeProvider *self,
                                                     SAFEARRAY **result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return *result ? S_OK : E_OUTOFMEMORY;
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
    HRESULT hr = root_available(root, &app);
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
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_set_percent(IScrollProvider *self,
    double horizontal, double vertical) {
    TintaUiaRoot *root = root_from_scroll(self);
    TintaApp *app;
    HRESULT hr = root_available(root, &app);
    if (FAILED(hr)) return hr;
    if (horizontal != UIA_ScrollPatternNoScroll)
        app->scroll_x = (float)(max(0.0, min(horizontal, 100.0)) / 100.0 *
            max(0.0f, app->content_width - app->width));
    if (vertical != UIA_ScrollPatternNoScroll)
        app->scroll_y = (float)(max(0.0, min(vertical, 100.0)) / 100.0 *
            max(0.0f, app->content_height - app->height));
    InvalidateRect(app->hwnd, NULL, FALSE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_horizontal_percent(
    IScrollProvider *self, double *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    HRESULT hr = root_available(root, &app); if (FAILED(hr)) return hr;
    if (!result) return E_POINTER;
    *result = scroll_percent(app->scroll_x, app->content_width, (float)app->width);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_vertical_percent(
    IScrollProvider *self, double *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    HRESULT hr = root_available(root, &app); if (FAILED(hr)) return hr;
    if (!result) return E_POINTER;
    *result = scroll_percent(app->scroll_y, app->content_height, (float)app->height);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_horizontal_view(
    IScrollProvider *self, double *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    HRESULT hr = root_available(root, &app); if (FAILED(hr)) return hr;
    if (!result) return E_POINTER;
    *result = app->content_width > 0 ? min(100.0,
        app->width / app->content_width * 100.0) : 100.0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_vertical_view(
    IScrollProvider *self, double *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    HRESULT hr = root_available(root, &app); if (FAILED(hr)) return hr;
    if (!result) return E_POINTER;
    *result = app->content_height > 0 ? min(100.0,
        app->height / app->content_height * 100.0) : 100.0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_horizontal_scrollable(
    IScrollProvider *self, BOOL *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    HRESULT hr = root_available(root, &app); if (FAILED(hr)) return hr;
    if (!result) return E_POINTER;
    *result = app->content_width > app->width;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_get_vertical_scrollable(
    IScrollProvider *self, BOOL *result) {
    TintaUiaRoot *root = root_from_scroll(self); TintaApp *app;
    HRESULT hr = root_available(root, &app); if (FAILED(hr)) return hr;
    if (!result) return E_POINTER;
    *result = app->content_height > app->height;
    return S_OK;
}

static IRawElementProviderSimpleVtbl root_simple_vtable = {
    root_simple_query, root_simple_add_ref, root_simple_release,
    root_get_provider_options, root_get_pattern, root_get_property, root_get_host
};

static IRawElementProviderFragmentVtbl root_fragment_vtable = {
    root_fragment_query, root_fragment_add_ref, root_fragment_release,
    root_fragment_navigate, root_fragment_runtime_id,
    root_fragment_bounding_rectangle, root_fragment_embedded_roots,
    root_fragment_set_focus, root_fragment_get_root
};

static IRawElementProviderFragmentRootVtbl root_fragment_root_vtable = {
    root_fragment_root_query, root_fragment_root_add_ref,
    root_fragment_root_release, root_element_from_point,
    root_fragment_get_focus
};

static ITextProviderVtbl root_text_vtable = {
    root_text_query, root_text_add_ref, root_text_release,
    text_get_selection, text_get_visible_ranges, text_range_from_child,
    text_range_from_point, text_get_document_range, text_get_selection_mode
};

static IScrollProviderVtbl root_scroll_vtable = {
    root_scroll_query, root_scroll_add_ref, root_scroll_release,
    scroll_action, scroll_set_percent, scroll_get_horizontal_percent,
    scroll_get_vertical_percent, scroll_get_horizontal_view,
    scroll_get_vertical_view, scroll_get_horizontal_scrollable,
    scroll_get_vertical_scrollable
};

static ITextRangeProviderVtbl range_vtable = {
    range_query_interface, range_add_ref, range_release, range_clone,
    range_compare, range_compare_endpoints, range_expand,
    range_find_attribute, range_find_text, range_get_attribute,
    range_get_rectangles, range_get_enclosing, range_get_text, range_move,
    range_move_endpoint, range_move_endpoint_range, range_select,
    range_add_selection, range_remove_selection, range_scroll_into_view,
    range_get_children
};

static IRawElementProviderSimpleVtbl child_simple_vtable = {
    child_simple_query, child_simple_add_ref, child_simple_release,
    child_get_provider_options, child_get_pattern, child_get_property,
    child_get_host
};

static IRawElementProviderFragmentVtbl child_fragment_vtable = {
    child_fragment_query, child_fragment_add_ref, child_fragment_release,
    child_navigate, child_runtime_id, child_bounding_rectangle,
    child_embedded_roots, child_set_focus, child_get_fragment_root
};

static IInvokeProviderVtbl child_invoke_vtable = {
    child_invoke_query, child_invoke_add_ref, child_invoke_release,
    child_invoke_action
};

static TintaUiaRoot *root_create(TintaApp *app) {
    TintaUiaRoot *root = (TintaUiaRoot *)calloc(1, sizeof(*root));
    if (!root) return NULL;
    root->simple.lpVtbl = &root_simple_vtable;
    root->fragment.lpVtbl = &root_fragment_vtable;
    root->fragment_root.lpVtbl = &root_fragment_root_vtable;
    root->text.lpVtbl = &root_text_vtable;
    root->scroll.lpVtbl = &root_scroll_vtable;
    root->references = 1;
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
    UiaDisconnectProvider(&root->simple);
    root->app = NULL;
    root_release(root);
}

void tinta_uia_raise_text_changed(TintaApp *app) {
    TintaUiaRoot *root = app ? (TintaUiaRoot *)app->uia_provider : NULL;
    if (root) UiaRaiseAutomationEvent(&root->simple,
                                      UIA_Text_TextChangedEventId);
}

void tinta_uia_raise_selection_changed(TintaApp *app) {
    TintaUiaRoot *root = app ? (TintaUiaRoot *)app->uia_provider : NULL;
    if (root) UiaRaiseAutomationEvent(&root->simple,
                                      UIA_Text_TextSelectionChangedEventId);
}
