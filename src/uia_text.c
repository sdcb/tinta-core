#include "uia_internal.h"

/* TextPattern and TextRangeProvider implementation over rendered document text. */

static HRESULT STDMETHODCALLTYPE root_text_query(
    ITextProvider *self, REFIID iid, void **result) {
    return tinta_uia_root_query(root_from_text(self), iid, result);
}

static ULONG STDMETHODCALLTYPE root_text_add_ref(ITextProvider *self) {
    return tinta_uia_root_add_ref(root_from_text(self));
}

static ULONG STDMETHODCALLTYPE root_text_release(ITextProvider *self) {
    return tinta_uia_root_release(root_from_text(self));
}


static TintaUiaRange *range_create(TintaUiaRoot *root,
                                   size_t start, size_t end) {
    TintaUiaRange *range;
    size_t length;
    if (FAILED(tinta_uia_root_available(root, NULL))) return NULL;
    length = root->app->doc_text.len;
    if (start > length) start = length;
    if (end > length) end = length;
    if (start > end) start = end;
    range = (TintaUiaRange *)calloc(1, sizeof(*range));
    if (!range) {
        tinta_uia_root_done(root);
        return NULL;
    }
    range->iface.lpVtbl = &tinta_uia_range_vtable;
    range->references = 1;
    range->root = root;
    range->start = start;
    range->end = end;
    range->revision = root->app->document_revision;
    tinta_uia_root_add_ref(root);
    tinta_uia_root_done(root);
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
    HRESULT hr = tinta_uia_root_available(root, &app);
    if (FAILED(hr)) return hr;
    range = range_create(root,
        min(app->selection_anchor, app->selection_focus),
        max(app->selection_anchor, app->selection_focus));
    if (!range) {
        tinta_uia_root_done(root);
        return E_OUTOFMEMORY;
    }
    hr = range_array(range, result);
    ITextRangeProvider_Release(&range->iface);
    tinta_uia_root_done(root);
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
    HRESULT hr = tinta_uia_root_available(root, &app);
    if (FAILED(hr)) return hr;
    end = 0;
    for (i = 0; i < app->text_runs.len; i++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun, app->text_runs, i);
        if (tinta_run_is_visually_exposed(app, run) &&
            run->y + run->height >= app->scroll_y &&
            run->y <= app->scroll_y + app->height) {
            if (!found) start = run->doc_start;
            end = run->doc_start + run->doc_length;
            found = true;
        }
    }
    range = range_create(root, start, end);
    if (!range) {
        tinta_uia_root_done(root);
        return E_OUTOFMEMORY;
    }
    hr = range_array(range, result);
    ITextRangeProvider_Release(&range->iface);
    tinta_uia_root_done(root);
    return hr;
}

static HRESULT STDMETHODCALLTYPE text_range_from_child(ITextProvider *self,
    IRawElementProviderSimple *child, ITextRangeProvider **result) {
    TintaUiaRoot *root = root_from_text(self);
    TintaUiaChild *semantic;
    TintaSemanticItem item;
    TintaUiaRange *range;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    if (!child || child->lpVtbl != &tinta_uia_child_simple_vtable) return E_INVALIDARG;
    semantic = child_from_simple(child);
    if (semantic->root != root) return E_INVALIDARG;
    hr = tinta_uia_root_available(root, NULL);
    if (FAILED(hr)) return hr;
    if (semantic->revision != root->app->document_revision ||
        !tinta_uia_semantic_item(root, semantic->index, &item)) {
        tinta_uia_root_done(root);
        return E_INVALIDARG;
    }
    range = range_create(root, item.start, item.end);
    if (!range) {
        tinta_uia_root_done(root);
        return E_OUTOFMEMORY;
    }
    *result = &range->iface;
    tinta_uia_root_done(root);
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
    hr = tinta_uia_root_available(root, &app);
    if (FAILED(hr)) return hr;
    client.x = (LONG)point.x;
    client.y = (LONG)point.y;
    ScreenToClient(app->hwnd, &client);
    tinta_hit_test(app, (float)client.x, (float)client.y, &position, NULL);
    range = range_create(root, position, position);
    if (!range) {
        tinta_uia_root_done(root);
        return E_OUTOFMEMORY;
    }
    *result = &range->iface;
    tinta_uia_root_done(root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_get_document_range(ITextProvider *self,
    ITextRangeProvider **result) {
    TintaUiaRoot *root = root_from_text(self);
    TintaUiaRange *range;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = tinta_uia_root_available(root, NULL);
    if (FAILED(hr)) return hr;
    range = range_create(root, 0, root->app->doc_text.len);
    if (!range) {
        tinta_uia_root_done(root);
        return E_OUTOFMEMORY;
    }
    *result = &range->iface;
    tinta_uia_root_done(root);
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
        tinta_uia_root_release(range->root);
        free(range);
    }
    return (ULONG)value;


}

static HRESULT range_valid(TintaUiaRange *range, TintaApp **app) {
    TintaApp *current = NULL;
    HRESULT hr;
    if (!range) return E_INVALIDARG;
    hr = tinta_uia_root_available(range->root, &current);
    if (FAILED(hr)) return hr;
    if (range->revision != current->document_revision)
    {
        tinta_uia_root_done(range->root);
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (range->start > current->doc_text.len ||
        range->end > current->doc_text.len || range->start > range->end)
    {
        tinta_uia_root_done(range->root);
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (app) *app = current;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_clone(ITextRangeProvider *self,
                                              ITextRangeProvider **result) {
    TintaUiaRange *range = range_from_interface(self);
    TintaUiaRange *copy;
    if (!result) return E_POINTER;
    *result = NULL;
    if (FAILED(range_valid(range, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    copy = range_create(range->root, range->start, range->end);
    if (!copy) {
        tinta_uia_root_done(range->root);
        return E_OUTOFMEMORY;
    }
    *result = &copy->iface;
    tinta_uia_root_done(range->root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_compare(ITextRangeProvider *self,
    ITextRangeProvider *other, BOOL *result) {
    TintaUiaRange *left = range_from_interface(self);
    TintaUiaRange *right;
    if (!other || !result || other->lpVtbl != &tinta_uia_range_vtable)
        return E_INVALIDARG;
    right = range_from_interface(other);
    if (FAILED(range_valid(left, NULL)))
        return UIA_E_ELEMENTNOTAVAILABLE;
    tinta_uia_root_done(left->root);
    if (FAILED(range_valid(right, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    *result = left->root == right->root && left->start == right->start &&
              left->end == right->end;
    tinta_uia_root_done(right->root);
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
    if (!other || !result || other->lpVtbl != &tinta_uia_range_vtable)
        return E_INVALIDARG;
    if (FAILED(range_valid(range_from_interface(self), NULL)))
        return UIA_E_ELEMENTNOTAVAILABLE;
    tinta_uia_root_done(range_from_interface(self)->root);
    if (FAILED(range_valid(range_from_interface(other), NULL)))
        return UIA_E_ELEMENTNOTAVAILABLE;
    left = range_endpoint(range_from_interface(self), endpoint);
    right = range_endpoint(range_from_interface(other), other_endpoint);
    *result = left < right ? -1 : left > right ? 1 : 0;
    tinta_uia_root_done(range_from_interface(other)->root);
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
    tinta_uia_root_done(range->root);
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
        query_length > range->end - range->start) {
        tinta_uia_root_done(range->root);
        return S_OK;
    }
    if (backward) {
        position = range->end - query_length;
        for (;;) {
            if ((ignore_case ? _wcsnicmp(app->doc_text.data + position, text,
                                         query_length) :
                               wcsncmp(app->doc_text.data + position, text,
                                       query_length)) == 0) break;
            if (position == range->start) {
                tinta_uia_root_done(range->root);
                return S_OK;
            }
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
        if (position + query_length > range->end) {
            tinta_uia_root_done(range->root);
            return S_OK;
        }
    }
    {
        TintaUiaRange *found = range_create(range->root, position,
                                            position + query_length);
        if (!found) {
            tinta_uia_root_done(range->root);
            return E_OUTOFMEMORY;
        }
        *result = &found->iface;
    }
    tinta_uia_root_done(range->root);
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
    bool exposed = false;
    size_t i;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = NULL;
    hr = range_valid(range, &app);
    if (FAILED(hr)) return hr;
    if (range->start != range->end) {
        for (i = 0; i < app->text_runs.len; i++) {
            TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun, app->text_runs, i);
            if (range->start < run->doc_start + run->doc_length &&
                range->end > run->doc_start &&
                tinta_run_is_visually_exposed(app, run)) {
                exposed = true;
                break;
            }
        }
    }
    array = SafeArrayCreateVector(VT_R8, 0, exposed ? 4 : 0);
    if (!array) {
        tinta_uia_root_done(range->root);
        return E_OUTOFMEMORY;
    }
    if (exposed) {
        GetWindowRect(app->hwnd, &rect);
        values[0] = rect.left;
        values[1] = rect.top;
        values[2] = rect.right - rect.left;
        values[3] = rect.bottom - rect.top;
        for (index = 0; index < 4; index++)
            SafeArrayPutElement(array, &index, &values[index]);
    }


    *result = array;
    tinta_uia_root_done(range->root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_get_enclosing(ITextRangeProvider *self,
    IRawElementProviderSimple **result) {
    TintaUiaRange *range = range_from_interface(self);
    if (!result) return E_POINTER;
    if (FAILED(range_valid(range, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    *result = &range->root->simple;
    tinta_uia_root_add_ref(range->root);
    tinta_uia_root_done(range->root);
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
    hr = *result || !length ? S_OK : E_OUTOFMEMORY;
    tinta_uia_root_done(range->root);
    return hr;
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
    tinta_uia_root_done(range->root);
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
    tinta_uia_root_done(range->root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_move_endpoint_range(
    ITextRangeProvider *self, enum TextPatternRangeEndpoint endpoint,
    ITextRangeProvider *other,
    enum TextPatternRangeEndpoint other_endpoint) {
    TintaUiaRange *range = range_from_interface(self);
    TintaUiaRange *target;
    size_t position;
    if (!other || other->lpVtbl != &tinta_uia_range_vtable)
        return E_INVALIDARG;
    target = range_from_interface(other);
    if (range->root != target->root) return E_INVALIDARG;
    if (FAILED(range_valid(range, NULL)))
        return UIA_E_ELEMENTNOTAVAILABLE;
    tinta_uia_root_done(range->root);
    if (FAILED(range_valid(target, NULL))) return UIA_E_ELEMENTNOTAVAILABLE;
    position = range_endpoint(target, other_endpoint);
    tinta_uia_root_done(target->root);
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
    tinta_uia_root_done(range->root);
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
restart:
    for (i = 0; i < app->text_runs.len; i++) {
        TintaTextRun *run = TINTA_VEC_PTR(TintaTextRun, app->text_runs, i);
        if (range->start <= run->doc_start + run->doc_length &&
            range->end >= run->doc_start) {
            if (tinta_expand_run_block(app, run)) {
                KillTimer(app->hwnd, TINTA_TIMER_BLOCK_ANIMATION);
                if (!tinta_layout_document(app)) {
                    tinta_uia_root_done(range->root);
                    return E_FAIL;
                }
                goto restart;
            }
            float target = align_top ? run->y : run->y - app->height + run->height;
            app->scroll_y = max(0.0f, min(target,
                max(0.0f, app->content_height - app->height)));
            if (!tinta_horizontal_region_scroll_run_into_view(app, run)) {
                app->scroll_x = max(0.0f, min(
                    run->x + run->width * 0.5f - app->width * 0.5f,
                    max(0.0f, app->content_width - app->width)));
            }
            InvalidateRect(app->hwnd, NULL, FALSE);
            break;
        }
    }
    tinta_uia_root_done(range->root);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_get_children(ITextRangeProvider *self,
                                                     SAFEARRAY **result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return *result ? S_OK : E_OUTOFMEMORY;
}

ITextProviderVtbl tinta_uia_text_vtable = {
    root_text_query, root_text_add_ref, root_text_release,
    text_get_selection, text_get_visible_ranges, text_range_from_child,
    text_range_from_point, text_get_document_range, text_get_selection_mode
};

ITextRangeProviderVtbl tinta_uia_range_vtable = {
    range_query_interface, range_add_ref, range_release, range_clone,
    range_compare, range_compare_endpoints, range_expand,
    range_find_attribute, range_find_text, range_get_attribute,
    range_get_rectangles, range_get_enclosing, range_get_text, range_move,
    range_move_endpoint, range_move_endpoint_range, range_select,
    range_add_selection, range_remove_selection, range_scroll_into_view,
    range_get_children
};
