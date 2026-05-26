# Changelog

## [v1.2.11.1] - 2026-05-16

### Added
- Synced upstream CrossInk v1.2.11 + v1.2.11.1 fixes and features (Lyra Carousel theme skipped; Flow + Minimal are the two carousel themes).
- New Minimal home theme — clean home page with one large cover, available alongside Flow.
- In-reader **Controls** menu opens the full Settings → Controls list without exiting the book; in-reader Reader Options now exposes "Manage Fonts" and "Customise Status Bar" actions.
- **Custom sleep timer picker** — `Time to Sleep` is now a 1–30-minute integer instead of fixed presets. Existing JSON settings migrate to the closest minute value; existing binary settings reset to 10 minutes on first boot.
- BMP viewer now shows Prev/Next button hints and accepts both bottom-rocker and side-input directions.
- KOReader Sync: long-press to clear all bookmarks, hold Select to delete a single bookmark.
- File Browser: long-press a book to delete its cache or mark it Finished / Unfinished.
- Custom font-manager actions split into "Download All" and "Update All" entries.
- Confirmation toast when deleting a book's cache from the reader.

### Changed
- Web file-transfer filename byte limit raised from 100 → 150; uploads now preserve the original file extension.
- Deep sleep entry now shuts WiFi down before waiting on the power-button release.
- Settings: removed the "Progress Bar Thickness" option (no longer adjustable).
- Settings: "Show Battery" toggle now actually hides/shows the top battery bar on every menu surface.
- X4 only: top battery bar moved 4 px lower for better balance with the wider top bezel.

### Fixed
- Landscape EPUB inline images no longer clip when the bottom edge overlaps the screen margin.
- SD-card font picker no longer reopens after selection; in-reader font-size changes now rebuild the page layout correctly.
- KOReader Sync: authentication and parsing fixes for Calibre-Web-Automated and other non-strict servers.
- EPUB rendering: characters from unsupported charsets no longer overlap; advance-table and prewarm now degrade gracefully under low memory.
- JPEG decoder: heap-aware allocation via `unique_ptr`, wider rendering envelope, progressive-JPEG detection.

## [v1.2.10] - 2026-05-11

### Added
- Added a `Recent Books View` setting so the dedicated Recent Books screen can switch between the classic list and a 3x3 cover grid.
- Added more flexible reader controls, including orientation-aware front/side button settings, nav-only or all-button front inversion, tilt page turn shortcuts, and side-button long-press rotation actions.
- Added a per-session auto page turn interval picker with values from 5 to 120 seconds.
- Added a file-browser Home/Back long-press action for toggling hidden files and folders.
- Added EPUB rendering and diagnostics improvements, including visible `<hr>` separators and heap logs around section rebuilds, image extraction, page serialization, and sleep-cache rebuilds.
- Added reader font coverage for block redactions, black-square ornaments, Greek category letters, and turned-comma punctuation (PR #104).
- Added simulator tools for testing sleep/wake behavior and smoke-testing common screens and EPUB reader menus.

### Changed
- Reduced Controls settings section spacing so the grouped controls fit better on X3 screens.
- Made front reader long-press actions trigger when the hold delay is reached while normal page turns still trigger on release.
- Used the fast EPUB spine/TOC indexing path for books with 300+ spine entries so heavily split books build `book.bin` faster on first open.
- Allowed the web file manager and WebDAV to browse dot-prefixed hidden files when hidden files are enabled, matching the device file browser.

### Fixed
- Fixed reader button and shortcut behavior, including X3 power-button wake filtering, folder delete long-press timing, and WiFi scan/connect screens that could not be exited while work was in progress.
- Fixed RoundedRaff home-menu, keyboard, and button-hint rendering issues so Settings remains reachable and compact labels no longer overlap or disappear.
- Fixed font and glyph handling by reducing persistent SD-card font advance-cache memory, releasing optional font caches before image extraction only when heap is tight, and showing a visible replacement symbol when compact UI fonts lack `U+FFFD`.
- Fixed KOReader Sync authentication diagnostics and an in-reader sync crash, including clearer handling when a server or proxy returns non-JSON content.
- Fixed EPUB text rendering for redactions, whitespace-only XHTML text nodes, simple black CSS span backgrounds, list bullets in `<li><p>...</p></li>` items, and very long base64-like text runs.
- Fixed EPUB image, thumbnail, and section-rebuild stability so image-heavy chapters use less temporary memory, scale images more reliably, avoid stale dimensions, and suppress optional image work earlier under heap pressure.
- Fixed EPUB low-memory and cache safety by skipping optional next-chapter indexing and sleep-page cache rebuilds when heap is tight, failing safely with a malformed-book warning and Home exit path, rebuilding incompatible fork-written caches, and handling low-memory CSS parsing, truncated SD writes, invalid serialized strings, and failed temp-cache promotion.
- Fixed a Home crash after clearing reading cache by skipping optional EPUB thumbnail rebuilds when the source EPUB cache is missing.
- Fixed reader prewarm behavior by skipping image decoding, keeping mixed-style font glyphs cached together, and avoiding section rebuilds for render-quality-only option changes.
- Fixed concurrent render/storage crashes by serializing `GfxRenderer` scratch-buffer access, shared SPI bus access, and failed SPI lock cleanup.
- Fixed Recent Books, EPUB/XTC thumbnail caches, deleted-folder metadata, and XTC cover scaling so cached book data stays in sync and grid covers fill their slots correctly.
- Fixed simulator build configuration so SDL2 and simulator-provided network/OTA shims compile cleanly.

## [v1.2.9.1] - 2026-05-03

### Changed
- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Fixed
- Fix power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions. Those that had short-press power button to act as sleep saw unstable behavior previously. This should be fixed now
- Fix a potential crash when using `Go to %` in EPUBs
- Fix a potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid
