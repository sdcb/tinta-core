# Changelog

## 1.3.0

- Added the independent `Tinta.TextEditor` editable Win32 control without
  Markdown parsing or viewer coupling.
- Added LF-only UTF-16 text handling across Win32 messages, explicit-length
  `TEM_*` APIs, IME input, paste, copy and UI Automation; CRLF and lone CR are
  normalized on input and explicit-length input rejects embedded NUL.
- Added a persistent chunked AVL rope, paged line index and bounded undo/redo
  history for large documents, with `size_t` text ranges and directional
  selections for documents beyond legacy EDIT message widths.
- Added incremental DirectWrite layout, Direct2D color-font emoji rendering,
  system EDIT colors/fonts, optional Tinta themes and overflow-only overlay
  scrollbars.
- Added multiline EDIT-compatible text, selection, line, scrolling, font,
  margin, tab-stop, read-only, modified, limit and undo messages plus standard
  `EN_*` command notifications and a built-in editing context menu.
- Added keyboard/IME editing, surrogate-safe navigation, double-click word
  selection and system-threshold triple-click logical-line selection. A
  non-final selected line includes its LF, and third-click dragging extends by
  complete logical lines while preserving selection direction.
- Added an editor-specific UI Automation provider with Edit control type and
  Text, Value and Scroll patterns, including LF ranges, selection, visible
  ranges, point lookup, bounding rectangles and read-only enforcement.
- Added editor document differential tests, HWND/message and clipboard tests,
  triple-click regression coverage, cross-thread UIA smoke coverage, an editor
  example and installed-package consumer validation.
- Bumped the public API and package version to 1.3.0 and added
  `TINTA_CAPABILITY_TEXT_EDITOR`. `TintaCoreInitialize` now registers both
  Tinta window classes atomically while retaining shared graphics and lifetime
  accounting.
- Fixed read-only heading queries during control notifications, preventing the
  demo's table of contents from being cleared when handling
  `TMN_DOCUMENTREADY`.

## 1.2.0

- Refactored the vendored md4c into a Tinta-specific parser fork with named
  callback ABI 1 and exact source ranges on every block, span, and text event.
- Moved math delimiter recognition, structured Tinta HTML, entity and NULL
  decoding, highlights, strikethrough, superscripts, and subscripts into md4c,
  removing the corresponding input rewriting and AST rescanning from
  `src/markdown.c`.
- Added native structured callbacks for `<details>`, `<summary>`, ruby and the
  supported Tinta HTML subset, with unified callback abort and fallback paths.
- Added table-driven parser event/range/fallback coverage, callback-abort tests,
  exhaustive allocation-failure injection, and standalone UTF-8, UTF-16 and
  md4c-html build tests for the fork.
- Added native inline and display Markdown math for `$...$`, `$$...$$`,
  `\\(...\\)` and `\\[...\\]`, preserving the original source delimiters,
  offsets, selection, search, copy and UI Automation text.
- Added a bounded pure-C TeX AST and box-layout engine for fractions, roots,
  scripts, scalable delimiters, large operators, accents, common symbols,
  styles, matrices, cases and aligned environments. Unsupported or malformed
  formulas fall back atomically to their original LaTeX.
- Added Cambria Math rendering through DirectWrite, with safe big-endian
  OpenType `MATH` table validation, glyph variant/assembly readers and a
  runtime fallback when a usable system math font is unavailable.
- Added baseline-aware inline formula placement and block-local horizontal
  scrolling for overwide display formulas.
- Fixed the document, Code, Mermaid, and SVG Copy buttons to scale their
  geometry, labels, icons, strokes, and hit targets with the document zoom.
- Added the default-on `TINTA_ENABLE_MATH` build option and
  `TINTA_CAPABILITY_MATH`; math-disabled builds retain delimiter recognition
  and render the original LaTeX source.
- Added TeX parser fuzzing and Markdown, OpenType, layout and interaction
  regression coverage for math.

## 1.1.0

- Added an opt-in Copy button at the document's top-right that scrolls with
  the content and copies the displayed revision's original Markdown source.
- Added runtime-configurable left, top, right, and bottom page margins through
  `TMM_SETPAGEMARGINS` and `TMM_GETPAGEMARGINS`.
- Moved wide code and Mermaid content into block-local horizontal scrolling,
  while retaining a 240-DIP minimum block width for very narrow hosts.
- Made source Copy/Copied controls a permanent part of the Code and Mermaid
  headers, without runtime option flags.
- Added animated header-driven collapse and expansion for Code and Mermaid
  blocks, with search and UIA access automatically revealing hidden content.
- Centered Mermaid node labels and synchronized shape-aware diamond, hexagon,
  circle, and rectangle sizing with the upstream renderer.
- Added Mermaid Subgraphs with nesting, group endpoints, centered titles,
  container styling, and upstream-compatible inherited direction semantics.
- Reworked Mermaid compound layout to emit shape-clipped cubic Direct2D paths,
  reducing crossings and removing multi-bend exterior routing lanes.
- Preserved combined nested inline styles, including emphasized and linked
  code spans, and removed the synthetic full-space gap after inline code.
- Added triple-click logical-line selection with whole-line drag extension.
- Added best-effort Direct2D SVG rendering for local, remote, and Data URI
  images, including source Copy and animated standalone-block headers.
- Added HTML `<sub>`/`<sup>` scripts and GitHub-style animated
  `<details>`/`<summary>` containers around ordinary Markdown blocks.

- Upstream `tinta` changes have been reviewed through commit `ac521d6`.

## 1.0.0

- Stabilized the public Win32 control ABI and added runtime version,
  capability, statistics, and resource-error reporting.
- Moved streamed Markdown parsing to a latest-only background pipeline while
  retaining HWND-thread document commits and incremental layout.
- Added document revisions for UI Automation ranges and semantic children,
  notification-time lifetime protection, transactional document/base-URI
  changes, and deterministic image-worker shutdown.
- Enforced Markdown depth/node, Mermaid node/edge, remote download byte, image
  pixel, resource-count, and download-concurrency limits before expensive work.
- Added process-wide bounded remote-image pixel caching and per-document
  Mermaid parse caching.
- Improved long-paragraph wrapping, search highlighting, viewport draw
  culling, scroll anchoring, duplicate-heading accessibility mapping, and
  theme font consistency.
- Added optional UIA/Mermaid/syntax/local-image/remote-image feature trimming,
  a pure-C normal build, package-consumer checks, parser fuzz/benchmark tools,
  and full/trimmed CI coverage.

## 0.3.0

- Added opt-in height autosizing with optional minimum and maximum heights.
- Added `TMN_AUTOSIZED` so container windows can reflow sibling controls.

## 0.2.0

- Added coalesced UTF-8 streaming messages and revision/content notifications.
- Cached local and remote image resolution, decode results and Direct2D images
  across streamed revisions.
- Shared the multi-threaded Direct2D factory, DirectWrite factory and font
  fallback across control instances.
- Updated the chat demo to send 128 deltas after local and remote images.

## 0.1.0

- Initial `Tinta.MarkdownView` Win32 control.
- Markdown, Mermaid, syntax highlighting, images, selection, search and TOC.
- Static and shared library builds with minimal and demo hosts.
