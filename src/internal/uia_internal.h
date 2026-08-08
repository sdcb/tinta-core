#ifndef TINTA_UIA_INTERNAL_H
#define TINTA_UIA_INTERNAL_H

#include "uia_provider.h"

#define __UIA_OtherConstants_MODULE_DEFINED__
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include <oleauto.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

/*
 * UIAutomationClient.h defines these IDs as C objects rather than declarations,
 * which prevents including it from multiple C translation units. Keep the small
 * provider-side subset local to this implementation instead.
 */
#define UIA_InvokePatternId                  10000
#define UIA_ScrollPatternId                  10004
#define UIA_TextPatternId                    10014
#define UIA_Text_TextSelectionChangedEventId 20014
#define UIA_Text_TextChangedEventId          20015
#define UIA_ControlTypePropertyId            30003
#define UIA_NamePropertyId                   30005
#define UIA_HasKeyboardFocusPropertyId       30008
#define UIA_IsKeyboardFocusablePropertyId    30009
#define UIA_IsEnabledPropertyId              30010
#define UIA_AutomationIdPropertyId           30011
#define UIA_ClassNamePropertyId              30012
#define UIA_IsControlElementPropertyId       30016
#define UIA_IsContentElementPropertyId       30017
#define UIA_NativeWindowHandlePropertyId     30020
#define UIA_IsOffscreenPropertyId            30022
#define UIA_FrameworkIdPropertyId            30024
#define UIA_HyperlinkControlTypeId           50005
#define UIA_DocumentControlTypeId            50030
#define UIA_HeaderControlTypeId              50034
#define UIA_ScrollPatternNoScroll            (-1.0)

typedef struct TintaUiaRoot TintaUiaRoot;

typedef struct TintaUiaChild {
    IRawElementProviderSimple simple;
    IRawElementProviderFragment fragment;
    IInvokeProvider invoke;
    LONG references;
    TintaUiaRoot *root;
    size_t index;
    uint64_t revision;
} TintaUiaChild;

typedef struct TintaUiaRange {
    ITextRangeProvider iface;
    LONG references;
    TintaUiaRoot *root;
    size_t start;
    size_t end;
    uint64_t revision;
} TintaUiaRange;

struct TintaUiaRoot {
    IRawElementProviderSimple simple;
    IRawElementProviderFragment fragment;
    IRawElementProviderFragmentRoot fragment_root;
    ITextProvider text;
    IScrollProvider scroll;
    LONG references;
    CRITICAL_SECTION guard;
    TintaApp *app;
};

typedef struct TintaSemanticItem {
    bool link;
    bool visually_exposed;
    const wchar_t *name;
    const char *url;
    size_t start;
    size_t end;
    float left;
    float top;
    float right;
    float bottom;
} TintaSemanticItem;

extern IRawElementProviderFragmentVtbl tinta_uia_root_fragment_vtable;
extern IRawElementProviderFragmentRootVtbl tinta_uia_root_fragment_root_vtable;
extern ITextProviderVtbl tinta_uia_text_vtable;
extern ITextRangeProviderVtbl tinta_uia_range_vtable;
extern IRawElementProviderSimpleVtbl tinta_uia_child_simple_vtable;
extern IRawElementProviderFragmentVtbl tinta_uia_child_fragment_vtable;
extern IInvokeProviderVtbl tinta_uia_child_invoke_vtable;

static inline TintaUiaRoot *root_from_simple(
    IRawElementProviderSimple *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, simple);
}

static inline TintaUiaRoot *root_from_text(ITextProvider *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, text);
}

static inline TintaUiaRoot *root_from_fragment(
    IRawElementProviderFragment *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, fragment);
}

static inline TintaUiaRoot *root_from_fragment_root(
    IRawElementProviderFragmentRoot *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, fragment_root);
}

static inline TintaUiaRoot *root_from_scroll(IScrollProvider *value) {
    return CONTAINING_RECORD(value, TintaUiaRoot, scroll);
}

static inline TintaUiaRange *range_from_interface(
    ITextRangeProvider *value) {
    return CONTAINING_RECORD(value, TintaUiaRange, iface);
}

static inline TintaUiaChild *child_from_simple(
    IRawElementProviderSimple *value) {
    return CONTAINING_RECORD(value, TintaUiaChild, simple);
}

static inline TintaUiaChild *child_from_fragment(
    IRawElementProviderFragment *value) {
    return CONTAINING_RECORD(value, TintaUiaChild, fragment);
}

static inline TintaUiaChild *child_from_invoke(IInvokeProvider *value) {
    return CONTAINING_RECORD(value, TintaUiaChild, invoke);
}

HRESULT tinta_uia_root_available(TintaUiaRoot *root, TintaApp **app);
void tinta_uia_root_done(TintaUiaRoot *root);
ULONG tinta_uia_root_add_ref(TintaUiaRoot *root);
ULONG tinta_uia_root_release(TintaUiaRoot *root);
HRESULT tinta_uia_root_query(TintaUiaRoot *root, REFIID iid, void **result);
HRESULT tinta_uia_variant_bstr(VARIANT *value, const wchar_t *text);
bool tinta_uia_semantic_item(TintaUiaRoot *root, size_t index,
                             TintaSemanticItem *item);

#endif
