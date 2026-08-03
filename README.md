# Tinta Core [![GitHub](https://img.shields.io/badge/GitHub-sdcb%2Ftinta--core-181717?logo=github)](https://github.com/sdcb/tinta-core) [![QQ](https://img.shields.io/badge/QQ_Group-495782587-52B6EF?style=social&logo=tencent-qq&logoColor=000&logoWidth=20)](http://qm.qq.com/cgi-bin/qm/qr?_wv=1027&k=mma4msRKd372Z6dWpmBp4JZ9RL4Jrf8X&authKey=gccTx0h0RaH5b8B8jtuPJocU7MgFRUznqbV%2FLgsKdsK8RqZE%2BOhnETQ7nYVTp1W0&noverify=0&group_code=495782587)

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

Optional viewer features can be removed at compile time. All five options are
enabled by default:

```bat
cmake -S . -B build-mini -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DTINTA_ENABLE_UIA=OFF ^
  -DTINTA_ENABLE_MERMAID=OFF ^
  -DTINTA_ENABLE_SYNTAX=OFF ^
  -DTINTA_ENABLE_REMOTE_IMAGES=OFF ^
  -DTINTA_ENABLE_LOCAL_IMAGES=OFF
cmake --build build-mini --target tinta_minimal
```

The trimmed control still supports Markdown parsing, Direct2D/DirectWrite
layout and drawing, selection, scrolling, autosizing and streaming updates.
Mermaid source falls back to a normal code block, code blocks use plain
monospace text without syntax coloring, and unavailable images fall back to a
clickable link. The link uses the alt text when present and the source URI
otherwise. `TMM_GETOPTIONS` does not report image capabilities that were
compiled out.

The examples include `tinta_minimal`, the feature-oriented `tinta_demo`, and
`tinta_chat_demo`. The chat demo hosts one Markdown control per message,
provides a scrolling left/right conversation layout, and sends 128 simulated
SSE deltas through the streaming API. Its first response also demonstrates a
cached local image and an optional remote image.

For parser hardening and performance work, configure
`TINTA_BUILD_FUZZERS=ON` and/or `TINTA_BUILD_BENCHMARKS=ON`. These targets are
off by default and do not add C++ to a normal library build.

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
- `TMM_STREAM_BEGIN`, `TMM_STREAM_APPEND`, `TMM_STREAM_END` and
  `TMM_STREAM_CANCEL` accept arbitrary UTF-8 delta boundaries. The control
  copies each delta, coalesces revisions at 20 Hz by default, parses the latest
  snapshot on a worker thread, commits it transactionally on the HWND thread,
  and reports displayed revisions with `TMN_STREAMUPDATED`.
- `TMM_SETAUTOSIZE` can fit the control height to its content and optionally
  cap it at a maximum height. Overflow continues to use the control's internal
  scroll bar, while `TMN_AUTOSIZED` lets an outer container reflow its layout.
- Input pointers are copied before `SendMessage` returns. Output messages use
  caller-owned buffers, so no allocator crosses a static-library or DLL ABI.
- The control owns parsing, Direct2D drawing, scrolling, selection, zoom,
  search highlights, TOC data, image decoding and UI Automation. File dialogs,
  persistence, search UI and context menus belong to the host.
- External links send `TMN_LINKACTIVATE`; returning zero permits the default
  `ShellExecuteW` behavior. Images send `TMN_RESOURCEOPENING`; the host can
  return default, block, or replace the resource URI.
- Local and remote image resolution, download failures, decoded WIC sources,
  and device-specific Direct2D bitmaps are cached per control for the lifetime
  of a streamed document. Successfully decoded remote pixel data also uses a
  bounded process cache so later controls do not download/decode it again.
  Image failures send `TMN_RESOURCEERROR`; image reflow sends
  `TMN_CONTENTUPDATED`.
- The default limits are 64 MiB of Markdown, one million AST nodes, an AST
  depth of 256, 10,000 Mermaid nodes, 20,000 Mermaid edges, 64 MiB per remote
  image, 64 million decoded pixels per image, 512 image resources and four
  concurrent downloads.
- `TMM_GETVERSION`, `TMM_GETCAPABILITIES`, and `TMM_GETSTATS` let hosts inspect
  the loaded build and current document without relying on compile-time
  assumptions.

`Tinta.MarkdownView` exposes the UI Automation Document, Text and Scroll
patterns. Headings and links appear as semantic children, and links implement
the Invoke pattern. Text ranges carry their source revision and become
unavailable after the document is replaced instead of reading unrelated text.
