#ifndef TINTA_WIN_GRAPHICS_C_H
#define TINTA_WIN_GRAPHICS_C_H

/* Keep the flattened COM layouts below authoritative under MinGW as well. */
#if defined(__MINGW32__)
#define ID2D1Factory TintaMingwID2D1Factory
#define ID2D1HwndRenderTarget TintaMingwID2D1HwndRenderTarget
#define ID2D1SolidColorBrush TintaMingwID2D1SolidColorBrush
#define ID2D1StrokeStyle TintaMingwID2D1StrokeStyle
#define ID2D1Bitmap TintaMingwID2D1Bitmap
#define ID2D1PathGeometry TintaMingwID2D1PathGeometry
#define ID2D1GeometrySink TintaMingwID2D1GeometrySink
#define ID2D1DeviceContext5 TintaMingwID2D1DeviceContext5
#define ID2D1SvgDocument TintaMingwID2D1SvgDocument
#endif

#define D2D_USE_C_DEFINITIONS
#include <d2d1.h>

#if defined(__MINGW32__)
#undef ID2D1Factory
#undef ID2D1HwndRenderTarget
#undef ID2D1SolidColorBrush
#undef ID2D1StrokeStyle
#undef ID2D1Bitmap
#undef ID2D1PathGeometry
#undef ID2D1GeometrySink
#undef ID2D1DeviceContext5
#undef ID2D1SvgDocument

typedef struct ID2D1Factory ID2D1Factory;
typedef struct ID2D1HwndRenderTarget ID2D1HwndRenderTarget;
typedef struct ID2D1SolidColorBrush ID2D1SolidColorBrush;
typedef struct ID2D1StrokeStyle ID2D1StrokeStyle;
typedef struct ID2D1Bitmap ID2D1Bitmap;
typedef struct ID2D1PathGeometry ID2D1PathGeometry;
typedef struct ID2D1GeometrySink ID2D1GeometrySink;
typedef struct ID2D1DeviceContext5 ID2D1DeviceContext5;
typedef struct ID2D1SvgDocument ID2D1SvgDocument;

/* MinGW's d2d1.h replaces this shared COM macro without restoring it. */
#undef DECLARE_INTERFACE
#undef DECLARE_INTERFACE_
#ifdef CONST_VTABLE
#define DECLARE_INTERFACE(iface)                                      \
    typedef interface iface { const struct iface##Vtbl *lpVtbl; } iface; \
    typedef const struct iface##Vtbl iface##Vtbl;                     \
    const struct iface##Vtbl
#else
#define DECLARE_INTERFACE(iface)                                \
    typedef interface iface { struct iface##Vtbl *lpVtbl; } iface; \
    typedef struct iface##Vtbl iface##Vtbl;                      \
    struct iface##Vtbl
#endif
#define DECLARE_INTERFACE_(iface, baseiface) DECLARE_INTERFACE(iface)
#endif

#include <wincodec.h>

typedef enum TintaDWriteFactoryType {
    TINTA_DWRITE_FACTORY_TYPE_SHARED = 0,
    TINTA_DWRITE_FACTORY_TYPE_ISOLATED = 1
} TintaDWriteFactoryType;

typedef enum TintaDWriteFontWeight {
    TINTA_DWRITE_FONT_WEIGHT_NORMAL = 400,
    TINTA_DWRITE_FONT_WEIGHT_SEMI_BOLD = 600,
    TINTA_DWRITE_FONT_WEIGHT_BOLD = 700
} TintaDWriteFontWeight;

typedef enum TintaDWriteFontStyle {
    TINTA_DWRITE_FONT_STYLE_NORMAL = 0,
    TINTA_DWRITE_FONT_STYLE_OBLIQUE = 1,
    TINTA_DWRITE_FONT_STYLE_ITALIC = 2
} TintaDWriteFontStyle;

typedef enum TintaDWriteFontStretch {
    TINTA_DWRITE_FONT_STRETCH_NORMAL = 5
} TintaDWriteFontStretch;

typedef enum TintaDWriteWordWrapping {
    TINTA_DWRITE_WORD_WRAPPING_WRAP = 0,
    TINTA_DWRITE_WORD_WRAPPING_NO_WRAP = 1
} TintaDWriteWordWrapping;

typedef enum TintaDWriteTextAlignment {
    TINTA_DWRITE_TEXT_ALIGNMENT_LEADING = 0,
    TINTA_DWRITE_TEXT_ALIGNMENT_TRAILING = 1,
    TINTA_DWRITE_TEXT_ALIGNMENT_CENTER = 2
} TintaDWriteTextAlignment;

typedef enum TintaDWriteParagraphAlignment {
    TINTA_DWRITE_PARAGRAPH_ALIGNMENT_NEAR = 0,
    TINTA_DWRITE_PARAGRAPH_ALIGNMENT_FAR = 1,
    TINTA_DWRITE_PARAGRAPH_ALIGNMENT_CENTER = 2
} TintaDWriteParagraphAlignment;

typedef enum TintaDWritePixelGeometry {
    TINTA_DWRITE_PIXEL_GEOMETRY_FLAT = 0,
    TINTA_DWRITE_PIXEL_GEOMETRY_RGB = 1,
    TINTA_DWRITE_PIXEL_GEOMETRY_BGR = 2
} TintaDWritePixelGeometry;

typedef enum TintaDWriteRenderingMode {
    TINTA_DWRITE_RENDERING_MODE_DEFAULT = 0,
    TINTA_DWRITE_RENDERING_MODE_ALIASED = 1,
    TINTA_DWRITE_RENDERING_MODE_GDI_CLASSIC = 2,
    TINTA_DWRITE_RENDERING_MODE_GDI_NATURAL = 3,
    TINTA_DWRITE_RENDERING_MODE_NATURAL = 4,
    TINTA_DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC = 5
} TintaDWriteRenderingMode;

typedef struct TintaDWriteUnicodeRange {
    UINT32 first;
    UINT32 last;
} TintaDWriteUnicodeRange;

typedef struct TintaDWriteTextMetrics {
    FLOAT left;
    FLOAT top;
    FLOAT width;
    FLOAT widthIncludingTrailingWhitespace;
    FLOAT height;
    FLOAT layoutWidth;
    FLOAT layoutHeight;
    UINT32 maxBidiReorderingDepth;
    UINT32 lineCount;
} TintaDWriteTextMetrics;

typedef struct TintaDWriteHitTestMetrics {
    UINT32 textPosition;
    UINT32 length;
    FLOAT left;
    FLOAT top;
    FLOAT width;
    FLOAT height;
    UINT32 bidiLevel;
    BOOL isText;
    BOOL isTrimmed;
} TintaDWriteHitTestMetrics;

typedef struct TintaDWriteClusterMetrics {
    FLOAT width;
    UINT16 length;
    UINT16 canWrapLineAfter : 1;
    UINT16 isWhitespace : 1;
    UINT16 isNewline : 1;
    UINT16 isSoftHyphen : 1;
    UINT16 isRightToLeft : 1;
    UINT16 padding : 11;
} TintaDWriteClusterMetrics;

typedef struct TintaDWriteLineMetrics {
    UINT32 length;
    UINT32 trailingWhitespaceLength;
    UINT32 newlineLength;
    FLOAT height;
    FLOAT baseline;
    BOOL isTrimmed;
} TintaDWriteLineMetrics;

typedef struct IDWriteFactory IDWriteFactory;
typedef struct IDWriteFactory2 IDWriteFactory2;
typedef struct IDWriteFontFallback IDWriteFontFallback;
typedef struct IDWriteFontFallbackBuilder IDWriteFontFallbackBuilder;
typedef struct IDWriteRenderingParams IDWriteRenderingParams;
typedef struct IDWriteTextLayout2 IDWriteTextLayout2;
typedef struct ID2D1DeviceContext5 ID2D1DeviceContext5;
typedef struct ID2D1SvgDocument ID2D1SvgDocument;
typedef void (STDMETHODCALLTYPE *TintaComMethod)(void);

typedef struct TintaIUnknownVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *self, REFIID iid, void **object);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *self);
    ULONG (STDMETHODCALLTYPE *Release)(void *self);
} TintaIUnknownVtbl;

typedef struct TintaIDWriteFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDWriteFactory *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDWriteFactory *);
    ULONG (STDMETHODCALLTYPE *Release)(IDWriteFactory *);
    TintaComMethod unused_before_rendering_params[7];
    HRESULT (STDMETHODCALLTYPE *CreateRenderingParams)(
        IDWriteFactory *, IDWriteRenderingParams **);
    TintaComMethod create_monitor_rendering_params;
    HRESULT (STDMETHODCALLTYPE *CreateCustomRenderingParams)(
        IDWriteFactory *, FLOAT, FLOAT, FLOAT, TintaDWritePixelGeometry,
        TintaDWriteRenderingMode, IDWriteRenderingParams **);
    TintaComMethod unused_before_create_text_format[2];
    HRESULT (STDMETHODCALLTYPE *CreateTextFormat)(
        IDWriteFactory *, const WCHAR *, void *, TintaDWriteFontWeight,
        TintaDWriteFontStyle, TintaDWriteFontStretch, FLOAT, const WCHAR *,
        IDWriteTextFormat **);
    TintaComMethod unused_before_create_text_layout[2];
    HRESULT (STDMETHODCALLTYPE *CreateTextLayout)(
        IDWriteFactory *, const WCHAR *, UINT32, IDWriteTextFormat *, FLOAT,
        FLOAT, IDWriteTextLayout **);
} TintaIDWriteFactoryVtbl;

struct IDWriteFactory {
    const TintaIDWriteFactoryVtbl *lpVtbl;
};

typedef struct TintaIDWriteTextFormatVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDWriteTextFormat *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDWriteTextFormat *);
    ULONG (STDMETHODCALLTYPE *Release)(IDWriteTextFormat *);
    HRESULT (STDMETHODCALLTYPE *SetTextAlignment)(IDWriteTextFormat *, TintaDWriteTextAlignment);
    HRESULT (STDMETHODCALLTYPE *SetParagraphAlignment)(IDWriteTextFormat *, TintaDWriteParagraphAlignment);
    HRESULT (STDMETHODCALLTYPE *SetWordWrapping)(IDWriteTextFormat *, TintaDWriteWordWrapping);
    TintaComMethod SetReadingDirection;
    TintaComMethod SetFlowDirection;
    HRESULT (STDMETHODCALLTYPE *SetIncrementalTabStop)(IDWriteTextFormat *, FLOAT);
    TintaComMethod unused_before_font_weight[13];
    TintaDWriteFontWeight (STDMETHODCALLTYPE *GetFontWeight)(IDWriteTextFormat *);
    TintaDWriteFontStyle (STDMETHODCALLTYPE *GetFontStyle)(IDWriteTextFormat *);
    TintaComMethod GetFontStretch;
    FLOAT (STDMETHODCALLTYPE *GetFontSize)(IDWriteTextFormat *);
} TintaIDWriteTextFormatVtbl;

struct IDWriteTextFormat {
    const TintaIDWriteTextFormatVtbl *lpVtbl;
};

typedef struct TintaIDWriteTextLayoutVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDWriteTextLayout *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDWriteTextLayout *);
    ULONG (STDMETHODCALLTYPE *Release)(IDWriteTextLayout *);
    HRESULT (STDMETHODCALLTYPE *SetTextAlignment)(IDWriteTextLayout *, TintaDWriteTextAlignment);
    HRESULT (STDMETHODCALLTYPE *SetParagraphAlignment)(IDWriteTextLayout *, TintaDWriteParagraphAlignment);
    HRESULT (STDMETHODCALLTYPE *SetWordWrapping)(IDWriteTextLayout *, TintaDWriteWordWrapping);
    TintaComMethod SetReadingDirection;
    TintaComMethod SetFlowDirection;
    HRESULT (STDMETHODCALLTYPE *SetIncrementalTabStop)(IDWriteTextLayout *, FLOAT);
    TintaComMethod unused_format_before_font_weight[13];
    TintaDWriteFontWeight (STDMETHODCALLTYPE *GetFontWeight)(IDWriteTextLayout *);
    TintaDWriteFontStyle (STDMETHODCALLTYPE *GetFontStyle)(IDWriteTextLayout *);
    TintaComMethod GetFontStretch;
    FLOAT (STDMETHODCALLTYPE *GetFontSize)(IDWriteTextLayout *);
    TintaComMethod unused_format_tail[2];
    TintaComMethod unused_layout_before_line_metrics[31];
    HRESULT (STDMETHODCALLTYPE *GetLineMetrics)(
        IDWriteTextLayout *, TintaDWriteLineMetrics *, UINT32, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *GetMetrics)(IDWriteTextLayout *, TintaDWriteTextMetrics *);
    TintaComMethod GetOverhangMetrics;
    HRESULT (STDMETHODCALLTYPE *GetClusterMetrics)(
        IDWriteTextLayout *, TintaDWriteClusterMetrics *, UINT32, UINT32 *);
    TintaComMethod DetermineMinWidth;
    HRESULT (STDMETHODCALLTYPE *HitTestPoint)(IDWriteTextLayout *, FLOAT, FLOAT,
                                              BOOL *, BOOL *, TintaDWriteHitTestMetrics *);
    HRESULT (STDMETHODCALLTYPE *HitTestTextPosition)(
        IDWriteTextLayout *, UINT32, BOOL, FLOAT *, FLOAT *, TintaDWriteHitTestMetrics *);
    HRESULT (STDMETHODCALLTYPE *HitTestTextRange)(
        IDWriteTextLayout *, UINT32, UINT32, FLOAT, FLOAT,
        TintaDWriteHitTestMetrics *, UINT32, UINT32 *);
} TintaIDWriteTextLayoutVtbl;

struct IDWriteTextLayout {
    const TintaIDWriteTextLayoutVtbl *lpVtbl;
};

typedef struct TintaIDWriteRenderingParamsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDWriteRenderingParams *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDWriteRenderingParams *);
    ULONG (STDMETHODCALLTYPE *Release)(IDWriteRenderingParams *);
    FLOAT (STDMETHODCALLTYPE *GetGamma)(IDWriteRenderingParams *);
    FLOAT (STDMETHODCALLTYPE *GetEnhancedContrast)(IDWriteRenderingParams *);
    FLOAT (STDMETHODCALLTYPE *GetClearTypeLevel)(IDWriteRenderingParams *);
    TintaDWritePixelGeometry (STDMETHODCALLTYPE *GetPixelGeometry)(IDWriteRenderingParams *);
    TintaDWriteRenderingMode (STDMETHODCALLTYPE *GetRenderingMode)(IDWriteRenderingParams *);
} TintaIDWriteRenderingParamsVtbl;

struct IDWriteRenderingParams {
    const TintaIDWriteRenderingParamsVtbl *lpVtbl;
};

typedef struct TintaIDWriteFactory2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDWriteFactory2 *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDWriteFactory2 *);
    ULONG (STDMETHODCALLTYPE *Release)(IDWriteFactory2 *);
    TintaComMethod unused_before_font_fallback[23];
    HRESULT (STDMETHODCALLTYPE *GetSystemFontFallback)(IDWriteFactory2 *, IDWriteFontFallback **);
    HRESULT (STDMETHODCALLTYPE *CreateFontFallbackBuilder)(IDWriteFactory2 *, IDWriteFontFallbackBuilder **);
} TintaIDWriteFactory2Vtbl;

struct IDWriteFactory2 {
    const TintaIDWriteFactory2Vtbl *lpVtbl;
};

typedef struct TintaIDWriteFontFallbackVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDWriteFontFallback *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDWriteFontFallback *);
    ULONG (STDMETHODCALLTYPE *Release)(IDWriteFontFallback *);
} TintaIDWriteFontFallbackVtbl;

struct IDWriteFontFallback {
    const TintaIDWriteFontFallbackVtbl *lpVtbl;
};

typedef struct TintaIDWriteFontFallbackBuilderVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDWriteFontFallbackBuilder *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDWriteFontFallbackBuilder *);
    ULONG (STDMETHODCALLTYPE *Release)(IDWriteFontFallbackBuilder *);
    HRESULT (STDMETHODCALLTYPE *AddMapping)(
        IDWriteFontFallbackBuilder *, const TintaDWriteUnicodeRange *, UINT32,
        const WCHAR **, UINT32, void *, const WCHAR *, const WCHAR *, FLOAT);
    HRESULT (STDMETHODCALLTYPE *AddMappings)(IDWriteFontFallbackBuilder *, IDWriteFontFallback *);
    HRESULT (STDMETHODCALLTYPE *CreateFontFallback)(IDWriteFontFallbackBuilder *, IDWriteFontFallback **);
} TintaIDWriteFontFallbackBuilderVtbl;

struct IDWriteFontFallbackBuilder {
    const TintaIDWriteFontFallbackBuilderVtbl *lpVtbl;
};

typedef struct TintaIDWriteTextLayout2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDWriteTextLayout2 *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDWriteTextLayout2 *);
    ULONG (STDMETHODCALLTYPE *Release)(IDWriteTextLayout2 *);
    TintaComMethod unused_before_font_fallback[75];
    HRESULT (STDMETHODCALLTYPE *SetFontFallback)(IDWriteTextLayout2 *, IDWriteFontFallback *);
} TintaIDWriteTextLayout2Vtbl;

struct IDWriteTextLayout2 {
    const TintaIDWriteTextLayout2Vtbl *lpVtbl;
};

typedef struct TintaID2D1FactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1Factory *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1Factory *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1Factory *);
    TintaComMethod unused_before_path_geometry[7];
    HRESULT (STDMETHODCALLTYPE *CreatePathGeometry)(ID2D1Factory *, ID2D1PathGeometry **);
    HRESULT (STDMETHODCALLTYPE *CreateStrokeStyle)(
        ID2D1Factory *, const D2D1_STROKE_STYLE_PROPERTIES *,
        const FLOAT *, UINT, ID2D1StrokeStyle **);
    TintaComMethod unused_before_hwnd_target[2];
    HRESULT (STDMETHODCALLTYPE *CreateHwndRenderTarget)(
        ID2D1Factory *, const D2D1_RENDER_TARGET_PROPERTIES *,
        const D2D1_HWND_RENDER_TARGET_PROPERTIES *, ID2D1HwndRenderTarget **);
} TintaID2D1FactoryVtbl;

struct ID2D1Factory {
    const TintaID2D1FactoryVtbl *lpVtbl;
};

typedef struct TintaID2D1HwndRenderTargetVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1HwndRenderTarget *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1HwndRenderTarget *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1HwndRenderTarget *);
    TintaComMethod resource_get_factory;
    TintaComMethod create_bitmap;
    HRESULT (STDMETHODCALLTYPE *CreateBitmapFromWicBitmap)(
        ID2D1HwndRenderTarget *, IWICBitmapSource *, const D2D1_BITMAP_PROPERTIES *,
        ID2D1Bitmap **);
    TintaComMethod unused_before_solid_brush[2];
    HRESULT (STDMETHODCALLTYPE *CreateSolidColorBrush)(
        ID2D1HwndRenderTarget *, const D2D1_COLOR_F *, const D2D1_BRUSH_PROPERTIES *,
        ID2D1SolidColorBrush **);
    TintaComMethod unused_before_draw_line[6];
    void (STDMETHODCALLTYPE *DrawLine)(ID2D1HwndRenderTarget *, D2D1_POINT_2F,
                                       D2D1_POINT_2F, ID2D1Brush *, FLOAT,
                                       ID2D1StrokeStyle *);
    void (STDMETHODCALLTYPE *DrawRectangle)(ID2D1HwndRenderTarget *, const D2D1_RECT_F *,
                                            ID2D1Brush *, FLOAT, ID2D1StrokeStyle *);
    void (STDMETHODCALLTYPE *FillRectangle)(ID2D1HwndRenderTarget *, const D2D1_RECT_F *,
                                            ID2D1Brush *);
    void (STDMETHODCALLTYPE *DrawRoundedRectangle)(
        ID2D1HwndRenderTarget *, const D2D1_ROUNDED_RECT *, ID2D1Brush *, FLOAT,
        ID2D1StrokeStyle *);
    void (STDMETHODCALLTYPE *FillRoundedRectangle)(
        ID2D1HwndRenderTarget *, const D2D1_ROUNDED_RECT *, ID2D1Brush *);
    void (STDMETHODCALLTYPE *DrawEllipse)(ID2D1HwndRenderTarget *, const D2D1_ELLIPSE *,
                                          ID2D1Brush *, FLOAT, ID2D1StrokeStyle *);
    void (STDMETHODCALLTYPE *FillEllipse)(ID2D1HwndRenderTarget *, const D2D1_ELLIPSE *,
                                          ID2D1Brush *);
    void (STDMETHODCALLTYPE *DrawGeometry)(ID2D1HwndRenderTarget *, ID2D1Geometry *,
                                           ID2D1Brush *, FLOAT, ID2D1StrokeStyle *);
    void (STDMETHODCALLTYPE *FillGeometry)(ID2D1HwndRenderTarget *, ID2D1Geometry *,
                                           ID2D1Brush *, ID2D1Brush *);
    TintaComMethod unused_before_draw_bitmap[2];
    void (STDMETHODCALLTYPE *DrawBitmap)(ID2D1HwndRenderTarget *, ID2D1Bitmap *,
                                         const D2D1_RECT_F *, FLOAT,
                                         D2D1_BITMAP_INTERPOLATION_MODE,
                                         const D2D1_RECT_F *);
    void (STDMETHODCALLTYPE *DrawText)(ID2D1HwndRenderTarget *, const WCHAR *, UINT,
                                       IDWriteTextFormat *, const D2D1_RECT_F *,
                                       ID2D1Brush *, D2D1_DRAW_TEXT_OPTIONS,
                                       DWRITE_MEASURING_MODE);
    void (STDMETHODCALLTYPE *DrawTextLayout)(ID2D1HwndRenderTarget *, D2D1_POINT_2F,
                                             IDWriteTextLayout *, ID2D1Brush *,
                                             D2D1_DRAW_TEXT_OPTIONS);
    TintaComMethod draw_glyph_run;
    void (STDMETHODCALLTYPE *SetTransform)(ID2D1HwndRenderTarget *,
                                           const D2D1_MATRIX_3X2_F *);
    void (STDMETHODCALLTYPE *GetTransform)(ID2D1HwndRenderTarget *,
                                           D2D1_MATRIX_3X2_F *);
    TintaComMethod antialias_mode[2];
    void (STDMETHODCALLTYPE *SetTextAntialiasMode)(ID2D1HwndRenderTarget *,
                                                   D2D1_TEXT_ANTIALIAS_MODE);
    TintaComMethod get_text_antialias_mode;
    void (STDMETHODCALLTYPE *SetTextRenderingParams)(ID2D1HwndRenderTarget *,
                                                      IDWriteRenderingParams *);
    TintaComMethod unused_before_clip[8];
    void (STDMETHODCALLTYPE *PushAxisAlignedClip)(ID2D1HwndRenderTarget *,
                                                  const D2D1_RECT_F *,
                                                  D2D1_ANTIALIAS_MODE);
    void (STDMETHODCALLTYPE *PopAxisAlignedClip)(ID2D1HwndRenderTarget *);
    void (STDMETHODCALLTYPE *Clear)(ID2D1HwndRenderTarget *, const D2D1_COLOR_F *);
    void (STDMETHODCALLTYPE *BeginDraw)(ID2D1HwndRenderTarget *);
    HRESULT (STDMETHODCALLTYPE *EndDraw)(ID2D1HwndRenderTarget *, D2D1_TAG *, D2D1_TAG *);
    TintaComMethod unused_render_target_tail[7];
    TintaComMethod check_window_state;
    HRESULT (STDMETHODCALLTYPE *Resize)(ID2D1HwndRenderTarget *, const D2D1_SIZE_U *);
} TintaID2D1HwndRenderTargetVtbl;

struct ID2D1HwndRenderTarget {
    const TintaID2D1HwndRenderTargetVtbl *lpVtbl;
};

typedef struct TintaID2D1SolidColorBrushVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1SolidColorBrush *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1SolidColorBrush *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1SolidColorBrush *);
    TintaComMethod unused_resource_and_brush[5];
    void (STDMETHODCALLTYPE *SetColor)(ID2D1SolidColorBrush *, const D2D1_COLOR_F *);
} TintaID2D1SolidColorBrushVtbl;

struct ID2D1SolidColorBrush {
    const TintaID2D1SolidColorBrushVtbl *lpVtbl;
};

typedef struct TintaID2D1StrokeStyleVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1StrokeStyle *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1StrokeStyle *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1StrokeStyle *);
} TintaID2D1StrokeStyleVtbl;

struct ID2D1StrokeStyle {
    const TintaID2D1StrokeStyleVtbl *lpVtbl;
};

typedef struct TintaID2D1BitmapVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1Bitmap *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1Bitmap *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1Bitmap *);
} TintaID2D1BitmapVtbl;

struct ID2D1Bitmap {
    const TintaID2D1BitmapVtbl *lpVtbl;
};

typedef struct TintaID2D1PathGeometryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1PathGeometry *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1PathGeometry *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1PathGeometry *);
    TintaComMethod unused_before_open[14];
    HRESULT (STDMETHODCALLTYPE *Open)(ID2D1PathGeometry *, ID2D1GeometrySink **);
} TintaID2D1PathGeometryVtbl;

struct ID2D1PathGeometry {
    const TintaID2D1PathGeometryVtbl *lpVtbl;
};

typedef struct TintaID2D1GeometrySinkVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1GeometrySink *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1GeometrySink *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1GeometrySink *);
    void (STDMETHODCALLTYPE *SetFillMode)(ID2D1GeometrySink *, D2D1_FILL_MODE);
    TintaComMethod set_segment_flags;
    void (STDMETHODCALLTYPE *BeginFigure)(ID2D1GeometrySink *, D2D1_POINT_2F,
                                          D2D1_FIGURE_BEGIN);
    void (STDMETHODCALLTYPE *AddLines)(ID2D1GeometrySink *, const D2D1_POINT_2F *, UINT);
    void (STDMETHODCALLTYPE *AddBeziers)(ID2D1GeometrySink *,
                                         const D2D1_BEZIER_SEGMENT *, UINT);
    void (STDMETHODCALLTYPE *EndFigure)(ID2D1GeometrySink *, D2D1_FIGURE_END);
    HRESULT (STDMETHODCALLTYPE *Close)(ID2D1GeometrySink *);
} TintaID2D1GeometrySinkVtbl;

struct ID2D1GeometrySink {
    const TintaID2D1GeometrySinkVtbl *lpVtbl;
};

typedef struct TintaID2D1DeviceContext5Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1DeviceContext5 *, REFIID,
                                                void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1DeviceContext5 *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1DeviceContext5 *);
    TintaComMethod inherited_methods[112];
    HRESULT (STDMETHODCALLTYPE *CreateSvgDocument)(
        ID2D1DeviceContext5 *, IStream *, D2D1_SIZE_F, ID2D1SvgDocument **);
    void (STDMETHODCALLTYPE *DrawSvgDocument)(ID2D1DeviceContext5 *,
                                              ID2D1SvgDocument *);
} TintaID2D1DeviceContext5Vtbl;

struct ID2D1DeviceContext5 {
    const TintaID2D1DeviceContext5Vtbl *lpVtbl;
};

typedef struct TintaID2D1SvgDocumentVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1SvgDocument *, REFIID,
                                                void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1SvgDocument *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1SvgDocument *);
} TintaID2D1SvgDocumentVtbl;

struct ID2D1SvgDocument {
    const TintaID2D1SvgDocumentVtbl *lpVtbl;
};

static const IID TINTA_IID_IDWRITE_FACTORY = {
    0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}
};

static const IID TINTA_IID_IDWRITE_FACTORY2 = {
    0x0439fc60, 0xca44, 0x4994, {0x8d, 0xee, 0x3a, 0x9a, 0xf7, 0xb7, 0x32, 0xec}
};

static const IID TINTA_IID_IDWRITE_TEXT_LAYOUT2 = {
    0x1093c18f, 0x8d5e, 0x43f0, {0xb0, 0x64, 0x09, 0x17, 0x31, 0x1b, 0x52, 0x5e}
};

static const IID TINTA_IID_ID2D1_DEVICE_CONTEXT = {
    0xe8f7fe7a, 0x191c, 0x466d, {0xad, 0x95, 0x97, 0x56, 0x78, 0xbd, 0xa9, 0x98}
};

static const IID TINTA_IID_ID2D1_DEVICE_CONTEXT5 = {
    0x7836d248, 0x68cc, 0x4df6, {0xb9, 0xe8, 0xde, 0x99, 0x1b, 0xf6, 0x2e, 0xb7}
};

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WINAPI DWriteCreateFactory(TintaDWriteFactoryType factory_type,
                                    REFIID iid, IUnknown **factory);

#ifdef __cplusplus
}
#endif

#endif
