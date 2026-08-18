# Tinta Core [![GitHub](https://img.shields.io/badge/GitHub-sdcb%2Ftinta--core-181717?logo=github)](https://github.com/sdcb/tinta-core) [![QQ](https://img.shields.io/badge/QQ_Group-495782587-52B6EF?style=social&logo=tencent-qq&logoColor=000&logoWidth=20)](http://qm.qq.com/cgi-bin/qm/qr?_wv=1027&k=mma4msRKd372Z6dWpmBp4JZ9RL4Jrf8X&authKey=gccTx0h0RaH5b8B8jtuPJocU7MgFRUznqbV%2FLgsKdsK8RqZE%2BOhnETQ7nYVTp1W0&noverify=0&group_code=495782587)

Tinta Core is a C11 Win32 control library. It exposes the read-only
`Tinta.MarkdownView` Markdown/math/Mermaid viewer and the independent editable
`Tinta.TextEditor` multiline text control. Both support normal
`CreateWindowExW` hosting and use ordinary Win32 messages and notifications.

Tinta Core is sdcb's reusable-control rewrite of Tinta C. Tinta C is itself
sdcb's pure C rewrite of the original Tinta project.

The original `tintac.exe` application is not part of this repository. This
repository contains the reusable controls and example hosts.

## Build

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Set `BUILD_SHARED_LIBS=ON` to build `tinta_core.dll`; the default is a static
library. md4c is vendored, so configuring the project requires no downloads.

Optional viewer features can be removed at compile time. All six options are
enabled by default:

```bat
cmake -S . -B build-mini -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DTINTA_ENABLE_UIA=OFF ^
  -DTINTA_ENABLE_MERMAID=OFF ^
  -DTINTA_ENABLE_SYNTAX=OFF ^
  -DTINTA_ENABLE_REMOTE_IMAGES=OFF ^
  -DTINTA_ENABLE_LOCAL_IMAGES=OFF ^
  -DTINTA_ENABLE_SVG=OFF ^
  -DTINTA_ENABLE_MATH=OFF
cmake --build build-mini --target tinta_minimal
```

The trimmed control still supports Markdown parsing, Direct2D/DirectWrite
layout and drawing, selection, scrolling, autosizing and streaming updates.
Mermaid source falls back to a normal code block, code blocks use plain
monospace text without syntax coloring, and unavailable images fall back to a
clickable link. The link uses the alt text when present and the source URI
otherwise. `TMM_GETOPTIONS` does not report image capabilities that were
compiled out. Math delimiters remain recognized when native math is disabled,
but formulas are displayed as their original LaTeX source.

The examples include `tinta_minimal`, the feature-oriented `tinta_demo`,
`tinta_chat_demo`, and `tinta_editor_demo`. The editor demo shows LF text,
word-wrap, read-only mode and system/Tinta theme switching. The chat demo hosts
one Markdown control per message,
provides a scrolling left/right conversation layout, and sends 128 simulated
SSE deltas through the streaming API. Its first response also demonstrates a
cached local image and an optional remote image.

For parser hardening and performance work, configure
`TINTA_BUILD_FUZZERS=ON` and/or `TINTA_BUILD_BENCHMARKS=ON`. These targets are
off by default and do not add C++ to a normal library build.
`TINTA_BUILD_LARGE_TESTS=ON` adds the separately labeled `editor_large`
100-MiB/million-line stress test; the default CI-sized editor tests use the
same document and edit paths with smaller inputs.

## Minimal use

```c
TintaCoreInitialize();
HWND view = CreateWindowExW(0, TINTA_MARKDOWN_VIEW_CLASSW, L"# Hello",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, width, height,
    parent, (HMENU)100, instance, NULL);
```

For UTF-8 input, document origins, Mermaid documents, themes, search, TOC and
notifications, include `tinta_core.h` and use the `TMM_*` message API.

## Editable text control

```c
TintaCoreInitialize();
HWND editor = CreateWindowExW(WS_EX_CLIENTEDGE,
    TINTA_TEXT_EDITOR_CLASSW, L"first\nsecond",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_WANTRETURN,
    x, y, width, height, parent, (HMENU)101, instance, NULL);
```

`Tinta.TextEditor` is independent of the Markdown viewer: it neither parses
Markdown nor maintains a preview connection. Text is UTF-16 and always uses a
single LF (`\n`) newline. `WM_SETTEXT`, paste, IME commits and the explicit
length `TEM_SETTEXTEX` message normalize CRLF and lone CR to LF; `WM_GETTEXT`,
`TEM_GETTEXTRANGE`, UI Automation and `CF_UNICODETEXT` copy return LF unchanged.

The editor uses a chunked AVL rope and a paged line index, so local edits and
position/line lookup do not copy or scan the complete document. DirectWrite
layout is measured incrementally, with the visible region rendered before the
rest of a large document. Direct2D drawing enables system color-font emoji.
Thin overlay scrollbars appear only when content overflows and the matching
`WS_VSCROLL`/`WS_HSCROLL` style permits them; wheel scrolling and caret reveal
continue to work when an overlay is hidden.

Common multiline EDIT messages are supported, including text, selection,
replace, line lookup, scrolling, font, margins, tab stops, read-only, modified,
text-limit and undo operations. `TEM_*` messages use `size_t` ranges for large
documents, preserve selection direction, configure wrap and the default
64-MiB undo budget, provide redo, expose content/scroll size, and select system,
built-in or custom Tinta themes. Standard `EN_*` notifications are sent through
`WM_COMMAND`.

Double-click selects a word. Triple-click uses the system multi-click time and
distance and selects the complete logical line: non-final lines include their
terminating LF, while a final line ends at the document end. Dragging after the
third click extends the selection by logical lines and preserves forward or
backward anchor/caret direction. The editor also provides an Undo/Cut/Copy/
Paste/Delete/Select All context menu, IME positioning, color emoji and basic
Edit Text/Value/Scroll UI Automation patterns. Version 1.3 does not include
syntax highlighting, OLE drag/drop, multiple selections or Markdown preview
linkage.

## Control contract

- Call `TintaCoreInitialize` before creating the first control and
  `TintaCoreUninitialize` after destroying the last one.
- `WM_SETTEXT` and `WM_GETTEXT` use UTF-16 Markdown. `TMM_SETDOCUMENT` accepts
  UTF-8, a Markdown/Mermaid format, and an optional local or HTTP base URI.
- `TMM_STREAM_BEGIN`, `TMM_STREAM_APPEND`, `TMM_STREAM_END` and
  `TMM_STREAM_CANCEL` accept arbitrary UTF-8 delta boundaries. The control
  copies each delta, coalesces revisions at 20 Hz by default, parses the latest
  snapshot on a worker thread, commits it transactionally on the HWND thread,
  and reports displayed revisions with `TMN_STREAMUPDATED`.
- `TMM_SETAUTOSIZE` can fit the control height to its content and optionally
  cap it at a maximum height. Overflow continues to use the control's internal
  scroll bar, while `TMN_AUTOSIZED` lets an outer container reflow its layout.
- `TMM_SETPAGEMARGINS` and `TMM_GETPAGEMARGINS` configure independent left,
  top, right, and bottom page margins in 96-DPI device-independent pixels.
  The defaults are 40, 20, 40, and 40 respectively.
- Input pointers are copied before `SendMessage` returns. Output messages use
  caller-owned buffers, so no allocator crosses a static-library or DLL ABI.
- The control owns parsing, Direct2D drawing, scrolling, selection, zoom,
  search highlights, TOC data, image decoding and UI Automation. File dialogs,
  persistence, search UI and context menus belong to the host.
- Double-click selects a word. Triple-click selects the complete logical text
  line, including its trailing newline, and dragging the third click extends
  the selection by whole logical lines rather than visual wraps.
- With keyboard navigation enabled, `Ctrl+0` resets zoom to 100% and
  `Ctrl++`/`Ctrl+-` adjust it by 10%. `Ctrl+mouse wheel` uses the same steps
  when mouse zoom is enabled.
- Code blocks show their Markdown language info verbatim in a non-selectable
  header. Blocks without a language are labeled `Plain text`; the Copy button
  shares the header and briefly changes to `Copied` after a successful copy.
- Code and successfully rendered Mermaid blocks always provide a source
  Copy/Copied button in their persistent headers. Mermaid headers are labeled
  `Mermaid`; parsed failures fall back to the normal code-block header.
- Code and Mermaid headers also include a chevron and toggle an animated
  collapse when clicked outside Copy. Collapsed blocks retain their source,
  selection text, search matches, and block-local horizontal position; search
  or UIA scrolling immediately expands hidden matches.
- Nested inline styles are merged instead of replacing one another. Inline
  code inside bold, italic, bold-italic, or links keeps the corresponding face
  and interaction, and punctuation after a code pill has no synthetic space.
- Inline and block HTML `<sub>`/`<sup>` map to native script text while
  preserving nested emphasis, links, code, selection, search, and copy.
- Inline `$...$` and `\\(...\\)` formulas participate in text flow with
  baseline-aware ascent and descent. Display `$$...$$` and `\\[...\\]`
  formulas occupy their own centered line, may span source lines, and receive
  block-local horizontal scrolling when wider than the viewport.
- Native math uses the system `Cambria Math` face and validates its OpenType
  `MATH` table before use. Fractions, roots, scripts, scalable delimiters,
  large operators, accents, common symbols and matrix/alignment environments
  are supported. Unknown commands, invalid structures, exhausted AST limits,
  or a missing/invalid math font fall back to the exact original LaTeX.
- GitHub-style `<details>`/`<summary>` can wrap Markdown blocks, including
  nested Details, Code, Mermaid, images, and SVG. Summary rows toggle with an
  animated chevron; `open` controls the initial state, and search or UIA
  access reveals hidden ancestor Details.
- Mermaid node labels are centered with shape-aware padding. Diamonds,
  hexagons, circles, and rectangles reserve dimensions appropriate to their
  outlines while retaining block-local horizontal scrolling.
- Mermaid flowcharts support nested Subgraphs, explicit IDs and titles,
  group endpoints, per-group directions, and `style`/`classDef` styling.
  A group keeps its local direction until one of its member nodes connects
  outside; in that case it inherits the parent direction, matching Mermaid.
  Compound layout keeps nested members inside their containers and emits
  shape-clipped Direct2D spline routes instead of multi-bend exterior lanes.
- Wide code and Mermaid blocks scroll horizontally inside the block instead
  of widening the document. Blocks keep a 240-DIP minimum width in extremely
  narrow hosts; their internal bars support dragging, horizontal wheel input,
  and Shift+wheel.
- `TINTA_OPTION_DOCUMENT_COPY_BUTTON` adds an opt-in Copy button at the
  document's top-right. It appears while the pointer is within the document's
  top 40 DIPs, scrolls away with the document, copies the exact source for the
  displayed revision, and briefly changes to `Copied`.
- External links send `TMN_LINKACTIVATE`; returning zero permits the default
  `ShellExecuteW` behavior. Images send `TMN_RESOURCEOPENING`; the host can
  return default, block, or replace the resource URI.
- Local and remote image resolution, download failures, decoded WIC sources,
  and device-specific Direct2D bitmaps are cached per control for the lifetime
  of a streamed document. Successfully decoded remote pixel data also uses a
  bounded process cache so later controls do not download/decode it again.
  Image failures send `TMN_RESOURCEERROR`; image reflow sends
  `TMN_CONTENTUPDATED`.
- SVG images support local files, HTTP(S) resources, and Base64 or
  percent-encoded `data:image/svg+xml` URIs. Standalone SVG paragraphs use a
  persistent `SVG` header with source Copy and animated collapse; inline SVG
  follows ordinary image behavior. Rendering uses the Direct2D SVG subset
  when `ID2D1DeviceContext5` is available and otherwise reports one resource
  error and falls back to a clickable alt-text or URI link. Intrinsic
  `width`/`height` and `viewBox` determine the natural size (default 300×150
  DIP); DTD and entity declarations are rejected.
- The default limits are 64 MiB of Markdown, one million AST nodes, an AST
  depth of 256, 10,000 Mermaid nodes, 20,000 Mermaid edges, 64 MiB per remote
  image, 64 million decoded pixels per image, 512 image resources and four
  concurrent downloads.
- `TMM_GETVERSION`, `TMM_GETCAPABILITIES`, and `TMM_GETSTATS` let hosts inspect
  the loaded build and current document without relying on compile-time
  assumptions. `TINTA_CAPABILITY_SVG` reports that SVG support was compiled
  in; rendering can still fall back when the current Windows version does not
  expose `ID2D1DeviceContext5`. `TINTA_CAPABILITY_MATH` reports that native
  math layout was compiled in; formulas still fall back when no usable
  Cambria Math `MATH` table is installed.

`Tinta.MarkdownView` exposes the UI Automation Document, Text and Scroll
patterns. Headings and links appear as semantic children, and links implement
the Invoke pattern. Text ranges carry their source revision and become
unavailable after the document is replaced instead of reading unrelated text.

`Tinta.TextEditor` exposes the UI Automation Edit control type with Text,
Value and Scroll patterns. Its ranges and values contain LF text, selection
changes are writable, and Value/selection mutation respects read-only state.
