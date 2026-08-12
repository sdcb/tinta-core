#include "editor.h"

#define __UIA_OtherConstants_MODULE_DEFINED__
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include <oleauto.h>

#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define UIA_ValuePatternId                    10002
#define UIA_ScrollPatternId                   10004
#define UIA_TextPatternId                     10014
#define UIA_Text_TextSelectionChangedEventId  20014
#define UIA_Text_TextChangedEventId            20015
#define UIA_AutomationFocusChangedEventId       20005
#define UIA_LayoutInvalidatedEventId            20008
#define UIA_ControlTypePropertyId              30003
#define UIA_NamePropertyId                     30005
#define UIA_HasKeyboardFocusPropertyId         30008
#define UIA_IsKeyboardFocusablePropertyId      30009
#define UIA_IsEnabledPropertyId                30010
#define UIA_AutomationIdPropertyId             30011
#define UIA_ClassNamePropertyId                30012
#define UIA_IsControlElementPropertyId         30016
#define UIA_IsContentElementPropertyId         30017
#define UIA_IsPasswordPropertyId               30019
#define UIA_NativeWindowHandlePropertyId       30020
#define UIA_FrameworkIdPropertyId              30024
#define UIA_ValueIsReadOnlyPropertyId           30046
#define UIA_IsReadOnlyAttributeId              40015
#define UIA_EditControlTypeId                  50004
#define UIA_ScrollPatternNoScroll              (-1.0)

typedef struct TintaEditorUiaRoot TintaEditorUiaRoot;

typedef struct TintaEditorUiaRange {
    ITextRangeProvider iface;
    LONG references;
    TintaEditorUiaRoot *root;
    size_t start;
    size_t end;
} TintaEditorUiaRange;

struct TintaEditorUiaRoot {
    IRawElementProviderSimple simple;
    ITextProvider text;
    IValueProvider value;
    IScrollProvider scroll;
    LONG references;
    CRITICAL_SECTION guard;
    HWND hwnd;
};

static IRawElementProviderSimpleVtbl editor_simple_vtable;
static ITextProviderVtbl editor_text_vtable;
static IValueProviderVtbl editor_value_vtable;
static IScrollProviderVtbl editor_scroll_vtable;
static ITextRangeProviderVtbl editor_range_vtable;

static TintaEditorUiaRoot *root_from_simple(IRawElementProviderSimple *value) {
    return CONTAINING_RECORD(value, TintaEditorUiaRoot, simple);
}

static TintaEditorUiaRoot *root_from_text(ITextProvider *value) {
    return CONTAINING_RECORD(value, TintaEditorUiaRoot, text);
}

static TintaEditorUiaRoot *root_from_value(IValueProvider *value) {
    return CONTAINING_RECORD(value, TintaEditorUiaRoot, value);
}

static TintaEditorUiaRoot *root_from_scroll(IScrollProvider *value) {
    return CONTAINING_RECORD(value, TintaEditorUiaRoot, scroll);
}

static TintaEditorUiaRange *range_from_iface(ITextRangeProvider *value) {
    return CONTAINING_RECORD(value, TintaEditorUiaRange, iface);
}

static ULONG root_add_ref(TintaEditorUiaRoot *root) {
    return (ULONG)InterlockedIncrement(&root->references);
}

static ULONG root_release(TintaEditorUiaRoot *root) {
    LONG references = InterlockedDecrement(&root->references);
    if (!references) {
        DeleteCriticalSection(&root->guard);
        free(root);
    }
    return (ULONG)references;
}

static HRESULT root_window(TintaEditorUiaRoot *root, HWND *hwnd) {
    HWND value;
    if (!root || !hwnd) return E_INVALIDARG;
    EnterCriticalSection(&root->guard);
    value = root->hwnd;
    LeaveCriticalSection(&root->guard);
    if (!value || !IsWindow(value)) return UIA_E_ELEMENTNOTAVAILABLE;
    *hwnd = value;
    return S_OK;
}

static HRESULT root_query(TintaEditorUiaRoot *root, REFIID iid,
                          void **result) {
    if (!result) return E_POINTER;
    *result = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) ||
        IsEqualIID(iid, &IID_IRawElementProviderSimple))
        *result = &root->simple;
    else if (IsEqualIID(iid, &IID_ITextProvider))
        *result = &root->text;
    else if (IsEqualIID(iid, &IID_IValueProvider))
        *result = &root->value;
    else if (IsEqualIID(iid, &IID_IScrollProvider))
        *result = &root->scroll;
    else
        return E_NOINTERFACE;
    root_add_ref(root);
    return S_OK;
}

static size_t editor_length(HWND hwnd) {
    LRESULT result = SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0);
    return result > 0 ? (size_t)result : 0;
}

static bool editor_selection(HWND hwnd, TintaEditorSelection *selection) {
    memset(selection, 0, sizeof(*selection));
    selection->cb_size = sizeof(*selection);
    return SendMessageW(hwnd, TEM_GETSELECTION, 0,
                        (LPARAM)selection) != 0;
}

static wchar_t *editor_copy_range(HWND hwnd, size_t start, size_t end) {
    TintaEditorTextRange range;
    wchar_t *text;
    size_t length;
    if (start > end) return NULL;
    length = end - start;
    if (length > (SIZE_MAX / sizeof(wchar_t)) - 1) return NULL;
    text = (wchar_t *)malloc((length + 1) * sizeof(wchar_t));
    if (!text) return NULL;
    memset(&range, 0, sizeof(range));
    range.cb_size = sizeof(range);
    range.start = start;
    range.end = end;
    range.text = text;
    range.text_capacity = length + 1;
    if ((size_t)SendMessageW(hwnd, TEM_GETTEXTRANGE, 0,
                             (LPARAM)&range) != length) {
        free(text);
        return NULL;
    }
    return text;
}

static HRESULT variant_bstr(VARIANT *value, const wchar_t *text) {
    V_VT(value) = VT_BSTR;
    V_BSTR(value) = SysAllocString(text ? text : L"");
    return V_BSTR(value) ? S_OK : E_OUTOFMEMORY;
}

static TintaEditorUiaRange *range_create(TintaEditorUiaRoot *root,
                                         size_t start, size_t end) {
    TintaEditorUiaRange *range;
    HWND hwnd;
    size_t length;
    if (FAILED(root_window(root, &hwnd))) return NULL;
    length = editor_length(hwnd);
    if (start > length) start = length;
    if (end > length) end = length;
    if (start > end) start = end;
    range = (TintaEditorUiaRange *)calloc(1, sizeof(*range));
    if (!range) return NULL;
    range->iface.lpVtbl = &editor_range_vtable;
    range->references = 1;
    range->root = root;
    range->start = start;
    range->end = end;
    root_add_ref(root);
    return range;
}

static HRESULT range_array(TintaEditorUiaRange *range, SAFEARRAY **result) {
    SAFEARRAY *array;
    LONG index = 0;
    IUnknown *unknown;
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

static HRESULT STDMETHODCALLTYPE simple_query(IRawElementProviderSimple *self,
                                               REFIID iid, void **result) {
    return root_query(root_from_simple(self), iid, result);
}

static ULONG STDMETHODCALLTYPE simple_add_ref(IRawElementProviderSimple *self) {
    return root_add_ref(root_from_simple(self));
}

static ULONG STDMETHODCALLTYPE simple_release(IRawElementProviderSimple *self) {
    return root_release(root_from_simple(self));
}

static HRESULT STDMETHODCALLTYPE simple_options(IRawElementProviderSimple *self,
                                                 enum ProviderOptions *result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = ProviderOptions_ServerSideProvider |
              ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE simple_pattern(IRawElementProviderSimple *self,
                                                 PATTERNID pattern,
                                                 IUnknown **result) {
    TintaEditorUiaRoot *root = root_from_simple(self);
    if (!result) return E_POINTER;
    *result = NULL;
    if (pattern == UIA_TextPatternId)
        *result = (IUnknown *)&root->text;
    else if (pattern == UIA_ValuePatternId)
        *result = (IUnknown *)&root->value;
    else if (pattern == UIA_ScrollPatternId)
        *result = (IUnknown *)&root->scroll;
    if (*result) root_add_ref(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE simple_property(IRawElementProviderSimple *self,
                                                  PROPERTYID property,
                                                  VARIANT *result) {
    TintaEditorUiaRoot *root = root_from_simple(self);
    HWND hwnd;
    HRESULT hr;
    if (!result) return E_POINTER;
    VariantInit(result);
    hr = root_window(root, &hwnd);
    if (FAILED(hr)) return hr;
    if (property == UIA_ControlTypePropertyId) {
        V_VT(result) = VT_I4;
        V_I4(result) = UIA_EditControlTypeId;
    } else if (property == UIA_NamePropertyId) {
        wchar_t name[256];
        GetWindowTextW(hwnd, name, ARRAYSIZE(name));
        hr = variant_bstr(result, name[0] ? name : L"Text editor");
    } else if (property == UIA_AutomationIdPropertyId ||
               property == UIA_ClassNamePropertyId) {
        hr = variant_bstr(result, TINTA_TEXT_EDITOR_CLASSW);
    } else if (property == UIA_FrameworkIdPropertyId) {
        hr = variant_bstr(result, L"Tinta");
    } else if (property == UIA_IsKeyboardFocusablePropertyId ||
               property == UIA_IsControlElementPropertyId ||
               property == UIA_IsContentElementPropertyId) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = VARIANT_TRUE;
    } else if (property == UIA_IsEnabledPropertyId) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = IsWindowEnabled(hwnd) ? VARIANT_TRUE : VARIANT_FALSE;
    } else if (property == UIA_HasKeyboardFocusPropertyId) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = GetFocus() == hwnd ? VARIANT_TRUE : VARIANT_FALSE;
    } else if (property == UIA_IsPasswordPropertyId) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = VARIANT_FALSE;
    } else if (property == UIA_ValueIsReadOnlyPropertyId) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) =
            (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY) ?
            VARIANT_TRUE : VARIANT_FALSE;
    } else if (property == UIA_NativeWindowHandlePropertyId) {
        V_VT(result) = VT_I4;
        V_I4(result) = (LONG)(LONG_PTR)hwnd;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE simple_host(IRawElementProviderSimple *self,
                                              IRawElementProviderSimple **result) {
    HWND hwnd;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = root_window(root_from_simple(self), &hwnd);
    return FAILED(hr) ? hr : UiaHostProviderFromHwnd(hwnd, result);
}

static HRESULT STDMETHODCALLTYPE text_query(ITextProvider *self, REFIID iid,
                                             void **result) {
    return root_query(root_from_text(self), iid, result);
}

static ULONG STDMETHODCALLTYPE text_add_ref(ITextProvider *self) {
    return root_add_ref(root_from_text(self));
}

static ULONG STDMETHODCALLTYPE text_release(ITextProvider *self) {
    return root_release(root_from_text(self));
}

static HRESULT STDMETHODCALLTYPE text_selection(ITextProvider *self,
                                                 SAFEARRAY **result) {
    TintaEditorUiaRoot *root = root_from_text(self);
    TintaEditorSelection selection;
    TintaEditorUiaRange *range;
    HWND hwnd;
    HRESULT hr = root_window(root, &hwnd);
    if (FAILED(hr)) return hr;
    if (!editor_selection(hwnd, &selection)) return E_FAIL;
    range = range_create(root, min(selection.anchor, selection.caret),
                         max(selection.anchor, selection.caret));
    if (!range) return E_OUTOFMEMORY;
    hr = range_array(range, result);
    ITextRangeProvider_Release(&range->iface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_visible(ITextProvider *self,
                                               SAFEARRAY **result) {
    TintaEditorUiaRoot *root = root_from_text(self);
    TintaEditorUiaRange *range;
    HWND hwnd;
    RECT client;
    TintaEditorHitTest first;
    TintaEditorHitTest last;
    HRESULT hr = root_window(root, &hwnd);
    if (FAILED(hr)) return hr;
    GetClientRect(hwnd, &client);
    memset(&first, 0, sizeof(first));
    memset(&last, 0, sizeof(last));
    last.point.x = client.right;
    last.point.y = client.bottom;
    SendMessageW(hwnd, TINTA_WM_EDITOR_HITTEST, 0, (LPARAM)&first);
    SendMessageW(hwnd, TINTA_WM_EDITOR_HITTEST, 0, (LPARAM)&last);
    range = range_create(root, first.position, last.position);
    if (!range) return E_OUTOFMEMORY;
    hr = range_array(range, result);
    ITextRangeProvider_Release(&range->iface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_from_child(ITextProvider *self,
    IRawElementProviderSimple *child, ITextRangeProvider **result) {
    (void)self; (void)child;
    if (!result) return E_POINTER;
    *result = NULL;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE text_from_point(ITextProvider *self,
    struct UiaPoint point, ITextRangeProvider **result) {
    TintaEditorUiaRoot *root = root_from_text(self);
    TintaEditorUiaRange *range;
    POINT client;
    TintaEditorHitTest hit;
    HWND hwnd;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = root_window(root, &hwnd);
    if (FAILED(hr)) return hr;
    client.x = (LONG)point.x;
    client.y = (LONG)point.y;
    ScreenToClient(hwnd, &client);
    memset(&hit, 0, sizeof(hit));
    hit.point = client;
    SendMessageW(hwnd, TINTA_WM_EDITOR_HITTEST, 0, (LPARAM)&hit);
    range = range_create(root, hit.position, hit.position);
    if (!range) return E_OUTOFMEMORY;
    *result = &range->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_document(ITextProvider *self,
                                                ITextRangeProvider **result) {
    TintaEditorUiaRoot *root = root_from_text(self);
    TintaEditorUiaRange *range;
    HWND hwnd;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = root_window(root, &hwnd);
    if (FAILED(hr)) return hr;
    range = range_create(root, 0, editor_length(hwnd));
    if (!range) return E_OUTOFMEMORY;
    *result = &range->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_supported(ITextProvider *self,
    enum SupportedTextSelection *result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = SupportedTextSelection_Single;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE value_query(IValueProvider *self, REFIID iid,
                                              void **result) {
    return root_query(root_from_value(self), iid, result);
}

static ULONG STDMETHODCALLTYPE value_add_ref(IValueProvider *self) {
    return root_add_ref(root_from_value(self));
}

static ULONG STDMETHODCALLTYPE value_release(IValueProvider *self) {
    return root_release(root_from_value(self));
}

static HRESULT STDMETHODCALLTYPE value_set(IValueProvider *self, LPCWSTR value) {
    TintaEditorUiaRoot *root = root_from_value(self);
    HWND hwnd;
    HRESULT hr = root_window(root, &hwnd);
    if (FAILED(hr)) return hr;
    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY)
        return UIA_E_INVALIDOPERATION;
    return SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)(value ? value : L"")) ?
        S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE value_get(IValueProvider *self, BSTR *result) {
    TintaEditorUiaRoot *root = root_from_value(self);
    wchar_t *text;
    size_t length;
    HWND hwnd;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = root_window(root, &hwnd);
    if (FAILED(hr)) return hr;
    length = editor_length(hwnd);
    text = editor_copy_range(hwnd, 0, length);
    if (!text) return length ? E_OUTOFMEMORY : (*result = SysAllocString(L"")) ? S_OK : E_OUTOFMEMORY;
    if (length > UINT_MAX) {
        free(text);
        return E_OUTOFMEMORY;
    }
    *result = SysAllocStringLen(text, (UINT)length);
    free(text);
    return *result || !length ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE value_read_only(IValueProvider *self,
                                                  BOOL *result) {
    HWND hwnd;
    HRESULT hr;
    if (!result) return E_POINTER;
    hr = root_window(root_from_value(self), &hwnd);
    if (FAILED(hr)) return hr;
    *result = (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY) != 0;
    return S_OK;
}

static double scroll_percent(float value, float content, float viewport) {
    float maximum = content - viewport;
    return maximum > 0.0f ? value / maximum * 100.0 : UIA_ScrollPatternNoScroll;
}

static HRESULT scroll_info(TintaEditorUiaRoot *root, HWND *hwnd,
                           TintaScrollPosition *position,
                           TintaContentSize *content, RECT *client) {
    HRESULT hr = root_window(root, hwnd);
    if (FAILED(hr)) return hr;
    memset(position, 0, sizeof(*position));
    memset(content, 0, sizeof(*content));
    position->cb_size = sizeof(*position);
    content->cb_size = sizeof(*content);
    if (!SendMessageW(*hwnd, TEM_GETSCROLLPOS, 0, (LPARAM)position) ||
        !SendMessageW(*hwnd, TEM_GETCONTENTSIZE, 0, (LPARAM)content))
        return E_FAIL;
    GetClientRect(*hwnd, client);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_query(IScrollProvider *self, REFIID iid,
                                               void **result) {
    return root_query(root_from_scroll(self), iid, result);
}

static ULONG STDMETHODCALLTYPE scroll_add_ref(IScrollProvider *self) {
    return root_add_ref(root_from_scroll(self));
}

static ULONG STDMETHODCALLTYPE scroll_release(IScrollProvider *self) {
    return root_release(root_from_scroll(self));
}

static HRESULT STDMETHODCALLTYPE scroll_action(IScrollProvider *self,
    enum ScrollAmount horizontal, enum ScrollAmount vertical) {
    TintaEditorUiaRoot *root = root_from_scroll(self);
    TintaScrollPosition position;
    TintaContentSize content;
    RECT client;
    HWND hwnd;
    HRESULT hr = scroll_info(root, &hwnd, &position, &content, &client);
    if (FAILED(hr)) return hr;
#define APPLY_SCROLL_AMOUNT(value, amount, small, large) \
    do { \
        if ((amount) == ScrollAmount_LargeDecrement) (value) -= (large); \
        else if ((amount) == ScrollAmount_SmallDecrement) (value) -= (small); \
        else if ((amount) == ScrollAmount_LargeIncrement) (value) += (large); \
        else if ((amount) == ScrollAmount_SmallIncrement) (value) += (small); \
    } while (0)
    APPLY_SCROLL_AMOUNT(position.x, horizontal, 42.0f,
                        (float)(client.right - client.left) * 0.8f);
    APPLY_SCROLL_AMOUNT(position.y, vertical, 42.0f,
                        (float)(client.bottom - client.top) * 0.8f);
#undef APPLY_SCROLL_AMOUNT
    position.x = max(0.0f, min(position.x,
        max(0.0f, content.width - (float)(client.right - client.left))));
    position.y = max(0.0f, min(position.y,
        max(0.0f, content.height - (float)(client.bottom - client.top))));
    return SendMessageW(hwnd, TEM_SETSCROLLPOS, 0, (LPARAM)&position) ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE scroll_set(IScrollProvider *self,
    double horizontal, double vertical) {
    TintaEditorUiaRoot *root = root_from_scroll(self);
    TintaScrollPosition position;
    TintaContentSize content;
    RECT client;
    HWND hwnd;
    HRESULT hr = scroll_info(root, &hwnd, &position, &content, &client);
    if (FAILED(hr)) return hr;
    if (horizontal != UIA_ScrollPatternNoScroll)
        position.x = (float)(max(0.0, min(horizontal, 100.0)) / 100.0 *
            max(0.0f, content.width - (float)(client.right - client.left)));
    if (vertical != UIA_ScrollPatternNoScroll)
        position.y = (float)(max(0.0, min(vertical, 100.0)) / 100.0 *
            max(0.0f, content.height - (float)(client.bottom - client.top)));
    return SendMessageW(hwnd, TEM_SETSCROLLPOS, 0, (LPARAM)&position) ? S_OK : E_FAIL;
}

static HRESULT scroll_get_data(IScrollProvider *self, int which,
                               double *number, BOOL *boolean) {
    TintaScrollPosition position;
    TintaContentSize content;
    RECT client;
    HWND hwnd;
    HRESULT hr = scroll_info(root_from_scroll(self), &hwnd, &position,
                             &content, &client);
    float width = (float)(client.right - client.left);
    float height = (float)(client.bottom - client.top);
    (void)hwnd;
    if (FAILED(hr)) return hr;
    if (number) {
        if (which == 0) *number = scroll_percent(position.x, content.width, width);
        else if (which == 1) *number = scroll_percent(position.y, content.height, height);
        else if (which == 2) *number = content.width > 0.0f ? min(100.0, width / content.width * 100.0) : 100.0;
        else *number = content.height > 0.0f ? min(100.0, height / content.height * 100.0) : 100.0;
    }
    if (boolean)
        *boolean = which == 4 ? content.width > width : content.height > height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_horizontal_percent(IScrollProvider *self,
                                                             double *result) {
    if (!result) return E_POINTER;
    return scroll_get_data(self, 0, result, NULL);
}

static HRESULT STDMETHODCALLTYPE scroll_vertical_percent(IScrollProvider *self,
                                                           double *result) {
    if (!result) return E_POINTER;
    return scroll_get_data(self, 1, result, NULL);
}

static HRESULT STDMETHODCALLTYPE scroll_horizontal_view(IScrollProvider *self,
                                                          double *result) {
    if (!result) return E_POINTER;
    return scroll_get_data(self, 2, result, NULL);
}

static HRESULT STDMETHODCALLTYPE scroll_vertical_view(IScrollProvider *self,
                                                        double *result) {
    if (!result) return E_POINTER;
    return scroll_get_data(self, 3, result, NULL);
}

static HRESULT STDMETHODCALLTYPE scroll_horizontal_enabled(IScrollProvider *self,
                                                             BOOL *result) {
    if (!result) return E_POINTER;
    return scroll_get_data(self, 4, NULL, result);
}

static HRESULT STDMETHODCALLTYPE scroll_vertical_enabled(IScrollProvider *self,
                                                           BOOL *result) {
    if (!result) return E_POINTER;
    return scroll_get_data(self, 5, NULL, result);
}

static HRESULT range_valid(TintaEditorUiaRange *range, HWND *hwnd) {
    size_t length;
    HRESULT hr = root_window(range ? range->root : NULL, hwnd);
    if (FAILED(hr)) return hr;
    length = editor_length(*hwnd);
    if (range->start > length) range->start = length;
    if (range->end > length) range->end = length;
    if (range->start > range->end) range->start = range->end;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_query(ITextRangeProvider *self,
                                              REFIID iid, void **result) {
    TintaEditorUiaRange *range = range_from_iface(self);
    if (!result) return E_POINTER;
    *result = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) &&
        !IsEqualIID(iid, &IID_ITextRangeProvider)) return E_NOINTERFACE;
    *result = self;
    InterlockedIncrement(&range->references);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE range_add_ref(ITextRangeProvider *self) {
    return (ULONG)InterlockedIncrement(&range_from_iface(self)->references);
}

static ULONG STDMETHODCALLTYPE range_release(ITextRangeProvider *self) {
    TintaEditorUiaRange *range = range_from_iface(self);
    LONG references = InterlockedDecrement(&range->references);
    if (!references) {
        root_release(range->root);
        free(range);
    }
    return (ULONG)references;
}

static HRESULT STDMETHODCALLTYPE range_clone(ITextRangeProvider *self,
                                              ITextRangeProvider **result) {
    TintaEditorUiaRange *range = range_from_iface(self);
    TintaEditorUiaRange *copy;
    HWND hwnd;
    if (!result) return E_POINTER;
    *result = NULL;
    if (FAILED(range_valid(range, &hwnd))) return UIA_E_ELEMENTNOTAVAILABLE;
    (void)hwnd;
    copy = range_create(range->root, range->start, range->end);
    if (!copy) return E_OUTOFMEMORY;
    *result = &copy->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_compare(ITextRangeProvider *self,
    ITextRangeProvider *other, BOOL *result) {
    TintaEditorUiaRange *left = range_from_iface(self);
    TintaEditorUiaRange *right;
    if (!other || other->lpVtbl != &editor_range_vtable || !result)
        return E_INVALIDARG;
    right = range_from_iface(other);
    *result = left->root == right->root && left->start == right->start &&
              left->end == right->end;
    return S_OK;
}

static size_t endpoint(TintaEditorUiaRange *range,
                       enum TextPatternRangeEndpoint which) {
    return which == TextPatternRangeEndpoint_Start ? range->start : range->end;
}

static HRESULT STDMETHODCALLTYPE range_compare_endpoints(ITextRangeProvider *self,
    enum TextPatternRangeEndpoint which, ITextRangeProvider *other,
    enum TextPatternRangeEndpoint other_which, int *result) {
    TintaEditorUiaRange *left = range_from_iface(self);
    TintaEditorUiaRange *right;
    size_t a, b;
    if (!other || other->lpVtbl != &editor_range_vtable || !result)
        return E_INVALIDARG;
    right = range_from_iface(other);
    if (left->root != right->root) return E_INVALIDARG;
    a = endpoint(left, which);
    b = endpoint(right, other_which);
    *result = a < b ? -1 : a > b ? 1 : 0;
    return S_OK;
}

static size_t move_position(const wchar_t *text, size_t length,
                            size_t position, enum TextUnit unit,
                            int direction) {
    if (unit == TextUnit_Document) return direction < 0 ? 0 : length;
    if (unit == TextUnit_Character) {
        if (direction < 0 && position) {
            position--;
            if (position && text[position] >= 0xdc00 && text[position] <= 0xdfff &&
                text[position - 1] >= 0xd800 && text[position - 1] <= 0xdbff)
                position--;
        } else if (direction > 0 && position < length) {
            if (text[position] >= 0xd800 && text[position] <= 0xdbff &&
                position + 1 < length && text[position + 1] >= 0xdc00 &&
                text[position + 1] <= 0xdfff) position += 2;
            else position++;
        }
        return position;
    }
    if (direction < 0) {
        if (position) position--;
        while (position && text[position - 1] != L'\n' &&
               (unit != TextUnit_Word || iswalnum(text[position - 1]) ||
                text[position - 1] == L'_')) position--;
    } else {
        while (position < length && text[position] != L'\n' &&
               (unit != TextUnit_Word || iswalnum(text[position]) ||
                text[position] == L'_')) position++;
        if (position < length) position++;
    }
    return position;
}

static HRESULT STDMETHODCALLTYPE range_expand(ITextRangeProvider *self,
                                               enum TextUnit unit) {
    TintaEditorUiaRange *range = range_from_iface(self);
    wchar_t *text;
    size_t length;
    HWND hwnd;
    HRESULT hr = range_valid(range, &hwnd);
    if (FAILED(hr)) return hr;
    length = editor_length(hwnd);
    text = editor_copy_range(hwnd, 0, length);
    if (!text) return length ? E_OUTOFMEMORY : S_OK;
    if (unit == TextUnit_Document) {
        range->start = 0;
        range->end = length;
    } else if (unit == TextUnit_Character) {
        range->end = move_position(text, length, range->start,
                                   TextUnit_Character, 1);
    } else {
        range->end = range->start;
        range->start = move_position(text, length, range->start, unit, -1);
        range->end = move_position(text, length, range->end, unit, 1);
    }
    free(text);
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
    BSTR query, BOOL backward, BOOL ignore_case, ITextRangeProvider **result) {
    TintaEditorUiaRange *range = range_from_iface(self);
    TintaEditorUiaRange *found;
    wchar_t *text;
    size_t query_length;
    size_t length;
    size_t position;
    HWND hwnd;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = range_valid(range, &hwnd);
    if (FAILED(hr)) return hr;
    query_length = query ? SysStringLen(query) : 0;
    length = range->end - range->start;
    if (!query_length || query_length > length) return S_OK;
    text = editor_copy_range(hwnd, range->start, range->end);
    if (!text) return E_OUTOFMEMORY;
    position = backward ? length - query_length : 0;
    for (;;) {
        int equal = ignore_case ? !_wcsnicmp(text + position, query, query_length) :
                                  !wcsncmp(text + position, query, query_length);
        if (equal) break;
        if ((!backward && position + query_length >= length) ||
            (backward && !position)) {
            free(text);
            return S_OK;
        }
        position = backward ? position - 1 : position + 1;
    }
    free(text);
    found = range_create(range->root, range->start + position,
                         range->start + position + query_length);
    if (!found) return E_OUTOFMEMORY;
    *result = &found->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_attribute(ITextRangeProvider *self,
    TEXTATTRIBUTEID attribute, VARIANT *result) {
    TintaEditorUiaRange *range = range_from_iface(self);
    IUnknown *not_supported = NULL;
    HWND hwnd;
    (void)attribute;
    if (!result) return E_POINTER;
    VariantInit(result);
    if (attribute == UIA_IsReadOnlyAttributeId &&
        SUCCEEDED(root_window(range->root, &hwnd))) {
        V_VT(result) = VT_BOOL;
        V_BOOL(result) = (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY) ?
            VARIANT_TRUE : VARIANT_FALSE;
    } else if (SUCCEEDED(UiaGetReservedNotSupportedValue(&not_supported))) {
        V_VT(result) = VT_UNKNOWN;
        V_UNKNOWN(result) = not_supported;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_rectangles(ITextRangeProvider *self,
                                                   SAFEARRAY **result) {
    TintaEditorUiaRange *range = range_from_iface(self);
    SAFEARRAY *array;
    TintaEditorPositionPoint start;
    TintaEditorPositionPoint end;
    LONG index;
    double values[4];
    HWND hwnd;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = range_valid(range, &hwnd);
    if (FAILED(hr)) return hr;
    memset(&start, 0, sizeof(start));
    memset(&end, 0, sizeof(end));
    start.position = range->start;
    end.position = range->end;
    SendMessageW(hwnd, TINTA_WM_EDITOR_POSITION_POINT, 0, (LPARAM)&start);
    SendMessageW(hwnd, TINTA_WM_EDITOR_POSITION_POINT, 0, (LPARAM)&end);
    ClientToScreen(hwnd, &start.point);
    ClientToScreen(hwnd, &end.point);
    array = SafeArrayCreateVector(VT_R8, 0, 4);
    if (!array) return E_OUTOFMEMORY;
    values[0] = start.point.x;
    values[1] = start.point.y;
    values[2] = max(1, end.point.x - start.point.x);
    values[3] = max(1, end.point.y - start.point.y + 20);
    for (index = 0; index < 4; index++)
        SafeArrayPutElement(array, &index, &values[index]);
    *result = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_enclosing(ITextRangeProvider *self,
    IRawElementProviderSimple **result) {
    TintaEditorUiaRange *range = range_from_iface(self);
    if (!result) return E_POINTER;
    *result = &range->root->simple;
    root_add_ref(range->root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_text(ITextRangeProvider *self,
                                             int maximum, BSTR *result) {
    TintaEditorUiaRange *range = range_from_iface(self);
    wchar_t *text;
    size_t length;
    HWND hwnd;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = range_valid(range, &hwnd);
    if (FAILED(hr)) return hr;
    length = range->end - range->start;
    if (maximum >= 0 && length > (size_t)maximum) length = (size_t)maximum;
    if (length > UINT_MAX) return E_OUTOFMEMORY;
    text = editor_copy_range(hwnd, range->start, range->start + length);
    if (!text) return length ? E_OUTOFMEMORY : (*result = SysAllocString(L"")) ? S_OK : E_OUTOFMEMORY;
    *result = SysAllocStringLen(text, (UINT)length);
    free(text);
    return *result || !length ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE range_move(ITextRangeProvider *self,
    enum TextUnit unit, int count, int *moved) {
    TintaEditorUiaRange *range = range_from_iface(self);
    wchar_t *text;
    size_t length;
    size_t position;
    int direction = count < 0 ? -1 : 1;
    int actual = 0;
    HWND hwnd;
    HRESULT hr;
    if (!moved) return E_POINTER;
    *moved = 0;
    hr = range_valid(range, &hwnd);
    if (FAILED(hr)) return hr;
    length = editor_length(hwnd);
    text = editor_copy_range(hwnd, 0, length);
    if (!text) return length ? E_OUTOFMEMORY : S_OK;
    position = range->start;
    while (actual != count) {
        size_t next = move_position(text, length, position, unit, direction);
        if (next == position) break;
        position = next;
        actual += direction;
    }
    free(text);
    range->start = range->end = position;
    *moved = actual;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_move_endpoint(ITextRangeProvider *self,
    enum TextPatternRangeEndpoint which, enum TextUnit unit, int count,
    int *moved) {
    TintaEditorUiaRange *range = range_from_iface(self);
    wchar_t *text;
    size_t length;
    size_t position;
    int direction = count < 0 ? -1 : 1;
    int actual = 0;
    HWND hwnd;
    HRESULT hr;
    if (!moved) return E_POINTER;
    *moved = 0;
    hr = range_valid(range, &hwnd);
    if (FAILED(hr)) return hr;
    length = editor_length(hwnd);
    text = editor_copy_range(hwnd, 0, length);
    if (!text) return length ? E_OUTOFMEMORY : S_OK;
    position = endpoint(range, which);
    while (actual != count) {
        size_t next = move_position(text, length, position, unit, direction);
        if (next == position) break;
        position = next;
        actual += direction;
    }
    free(text);
    if (which == TextPatternRangeEndpoint_Start) {
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
    ITextRangeProvider *self, enum TextPatternRangeEndpoint which,
    ITextRangeProvider *other, enum TextPatternRangeEndpoint other_which) {
    TintaEditorUiaRange *range = range_from_iface(self);
    TintaEditorUiaRange *target;
    size_t position;
    if (!other || other->lpVtbl != &editor_range_vtable) return E_INVALIDARG;
    target = range_from_iface(other);
    if (range->root != target->root) return E_INVALIDARG;
    position = endpoint(target, other_which);
    if (which == TextPatternRangeEndpoint_Start) {
        range->start = position;
        if (range->start > range->end) range->end = range->start;
    } else {
        range->end = position;
        if (range->end < range->start) range->start = range->end;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_select(ITextRangeProvider *self) {
    TintaEditorUiaRange *range = range_from_iface(self);
    TintaEditorSelection selection;
    HWND hwnd;
    HRESULT hr = range_valid(range, &hwnd);
    if (FAILED(hr)) return hr;
    memset(&selection, 0, sizeof(selection));
    selection.cb_size = sizeof(selection);
    selection.anchor = range->start;
    selection.caret = range->end;
    return SendMessageW(hwnd, TEM_SETSELECTION, 0, (LPARAM)&selection) ? S_OK : E_FAIL;
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
    TintaEditorUiaRange *range = range_from_iface(self);
    TintaEditorSelection old_selection;
    TintaEditorSelection selection;
    HWND hwnd;
    HRESULT hr = range_valid(range, &hwnd);
    (void)align_top;
    if (FAILED(hr)) return hr;
    if (!editor_selection(hwnd, &old_selection)) return E_FAIL;
    selection = old_selection;
    selection.anchor = selection.caret = range->start;
    SendMessageW(hwnd, TEM_SETSELECTION, 0, (LPARAM)&selection);
    SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
    SendMessageW(hwnd, TEM_SETSELECTION, 0, (LPARAM)&old_selection);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_children(ITextRangeProvider *self,
                                                 SAFEARRAY **result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return *result ? S_OK : E_OUTOFMEMORY;
}

static IRawElementProviderSimpleVtbl editor_simple_vtable = {
    simple_query, simple_add_ref, simple_release, simple_options,
    simple_pattern, simple_property, simple_host
};

static ITextProviderVtbl editor_text_vtable = {
    text_query, text_add_ref, text_release, text_selection, text_visible,
    text_from_child, text_from_point, text_document, text_supported
};

static IValueProviderVtbl editor_value_vtable = {
    value_query, value_add_ref, value_release, value_set, value_get,
    value_read_only
};

static IScrollProviderVtbl editor_scroll_vtable = {
    scroll_query, scroll_add_ref, scroll_release, scroll_action, scroll_set,
    scroll_horizontal_percent, scroll_vertical_percent,
    scroll_horizontal_view, scroll_vertical_view,
    scroll_horizontal_enabled, scroll_vertical_enabled
};

static ITextRangeProviderVtbl editor_range_vtable = {
    range_query, range_add_ref, range_release, range_clone, range_compare,
    range_compare_endpoints, range_expand, range_find_attribute,
    range_find_text, range_attribute, range_rectangles, range_enclosing,
    range_text, range_move, range_move_endpoint, range_move_endpoint_range,
    range_select, range_add_selection, range_remove_selection,
    range_scroll_into_view, range_children
};

static TintaEditorUiaRoot *root_create(TintaEditor *editor) {
    TintaEditorUiaRoot *root =
        (TintaEditorUiaRoot *)calloc(1, sizeof(*root));
    if (!root) return NULL;
    root->simple.lpVtbl = &editor_simple_vtable;
    root->text.lpVtbl = &editor_text_vtable;
    root->value.lpVtbl = &editor_value_vtable;
    root->scroll.lpVtbl = &editor_scroll_vtable;
    root->references = 1;
    root->hwnd = editor->hwnd;
    InitializeCriticalSection(&root->guard);
    return root;
}

LRESULT tinta_editor_uia_get_object(TintaEditor *editor,
                                    WPARAM wparam, LPARAM lparam) {
    TintaEditorUiaRoot *root;
    if (!editor || lparam != UiaRootObjectId) return 0;
    root = (TintaEditorUiaRoot *)editor->uia_provider;
    if (!root) {
        root = root_create(editor);
        if (!root) return 0;
        editor->uia_provider = root;
    }
    return UiaReturnRawElementProvider(editor->hwnd, wparam, lparam,
                                       &root->simple);
}

void tinta_editor_uia_disconnect(TintaEditor *editor) {
    TintaEditorUiaRoot *root;
    if (!editor || !editor->uia_provider) return;
    root = (TintaEditorUiaRoot *)editor->uia_provider;
    editor->uia_provider = NULL;
    EnterCriticalSection(&root->guard);
    root->hwnd = NULL;
    LeaveCriticalSection(&root->guard);
    UiaDisconnectProvider(&root->simple);
    root_release(root);
}

static void raise_event(TintaEditor *editor, EVENTID event_id) {
    TintaEditorUiaRoot *root = editor ?
        (TintaEditorUiaRoot *)editor->uia_provider : NULL;
    if (root) UiaRaiseAutomationEvent(&root->simple, event_id);
}

void tinta_editor_uia_raise_text_changed(TintaEditor *editor) {
    raise_event(editor, UIA_Text_TextChangedEventId);
}

void tinta_editor_uia_raise_selection_changed(TintaEditor *editor) {
    raise_event(editor, UIA_Text_TextSelectionChangedEventId);
}

void tinta_editor_uia_raise_focus_changed(TintaEditor *editor) {
    raise_event(editor, UIA_AutomationFocusChangedEventId);
}

void tinta_editor_uia_raise_scroll_changed(TintaEditor *editor) {
    raise_event(editor, UIA_LayoutInvalidatedEventId);
}

void tinta_editor_uia_raise_read_only_changed(TintaEditor *editor,
                                              bool old_value,
                                              bool new_value) {
    TintaEditorUiaRoot *root = editor ?
        (TintaEditorUiaRoot *)editor->uia_provider : NULL;
    VARIANT old_variant;
    VARIANT new_variant;
    if (!root || old_value == new_value) return;
    VariantInit(&old_variant);
    VariantInit(&new_variant);
    V_VT(&old_variant) = VT_BOOL;
    V_BOOL(&old_variant) = old_value ? VARIANT_TRUE : VARIANT_FALSE;
    V_VT(&new_variant) = VT_BOOL;
    V_BOOL(&new_variant) = new_value ? VARIANT_TRUE : VARIANT_FALSE;
    UiaRaiseAutomationPropertyChangedEvent(
        &root->simple, UIA_ValueIsReadOnlyPropertyId,
        old_variant, new_variant);
}
