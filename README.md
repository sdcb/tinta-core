# Tinta Core

Tinta Core is a C11 Win32 Markdown and Mermaid viewing control. It exposes the
window class `Tinta.MarkdownView`, supports normal `CreateWindowExW` hosting,
and uses window messages and `WM_NOTIFY` for integration.

Tinta Core is sdcb's reusable-control rewrite of Tinta C. Tinta C is itself
sdcb's pure C rewrite of the original Tinta project.

The original `tintac.exe` application is not part of this repository. This
repository contains the reusable read-only control, a minimal host, and a
larger demonstration host.

## Build

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Set `BUILD_SHARED_LIBS=ON` to build `tinta_core.dll`; the default is a static
library. md4c is vendored, so configuring the project requires no downloads.

## Minimal use

```c
TintaCoreInitialize();
HWND view = CreateWindowExW(0, TINTA_MARKDOWN_VIEW_CLASSW, L"# Hello",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, width, height,
    parent, (HMENU)100, instance, NULL);
```

For UTF-8 input, document origins, Mermaid documents, themes, search, TOC and
notifications, include `tinta_core.h` and use the `TMM_*` message API.

## Control contract

- Call `TintaCoreInitialize` before creating the first control and
  `TintaCoreUninitialize` after destroying the last one.
- `WM_SETTEXT` and `WM_GETTEXT` use UTF-16 Markdown. `TMM_SETDOCUMENT` accepts
  UTF-8, a Markdown/Mermaid format, and an optional local or HTTP base URI.
- Input pointers are copied before `SendMessage` returns. Output messages use
  caller-owned buffers, so no allocator crosses a static-library or DLL ABI.
- The control owns parsing, Direct2D drawing, scrolling, selection, zoom,
  search highlights, TOC data, image decoding and UI Automation. File dialogs,
  persistence, search UI and context menus belong to the host.
- External links send `TMN_LINKACTIVATE`; returning zero permits the default
  `ShellExecuteW` behavior. Images send `TMN_RESOURCEOPENING`; the host can
  return default, block, or replace the resource URI.
- The default limits are 64 MiB of Markdown, one million AST nodes, 64 million
  decoded pixels per image, 512 image resources and four concurrent downloads.

`Tinta.MarkdownView` exposes the UI Automation Document, Text and Scroll
patterns. Headings and links appear as semantic children, and links implement
the Invoke pattern.
