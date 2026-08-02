# Changelog

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
