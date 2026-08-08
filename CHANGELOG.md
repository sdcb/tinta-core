# Changelog

## 1.1.0

- Added an opt-in Copy button at the document's top-right that scrolls with
  the content and copies the displayed revision's original Markdown source.
- Added runtime-configurable left, top, right, and bottom page margins through
  `TMM_SETPAGEMARGINS` and `TMM_GETPAGEMARGINS`.
- Moved wide code and Mermaid content into block-local horizontal scrolling,
  while retaining a 240-DIP minimum block width for very narrow hosts.

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
