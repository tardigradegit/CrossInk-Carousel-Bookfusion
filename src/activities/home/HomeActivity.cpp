#include "HomeActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Txt.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

#include "../reader/BookReadingStats.h"
#include "../reader/BookStatsActivity.h"
#include "BookmarkStore.h"
#include "BookmarksHomeActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/chart.h"
#include "components/icons/folder.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr uint32_t TXT_CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t TXT_CACHE_VERSION = 2;

// Draw a 1-bit packed icon scaled with nearest-neighbor blocks. drawIcon
// doesn't scale, and baking pre-scaled assets into flash would balloon their
// size, so this trades a pile of small fillRects for keeping the existing
// 32×32 sources.
//
// Important: the icon bytes are stored in eink-native orientation
// (LandscapeCounterClockwise — the panel's physical layout). The renderer
// translates draw coordinates to native via Portrait rotation
// `native = (logical_y, panelHeight-1-logical_x)`. To read the right pixel
// for a logical destination, we map logical (lx, ly) within the icon to
// native (nx=ly, ny=srcSize-1-lx) and read the bit from native row-major
// storage. Without this mapping the icon renders rotated 90° CCW.
void drawIconScaled(const GfxRenderer& renderer, const uint8_t* iconData, int srcSize, int dstX, int dstY,
                    int dstSize) {
  for (int slx = 0; slx < srcSize; ++slx) {
    const int dstXStart = (slx * dstSize) / srcSize;
    const int dstXEnd = ((slx + 1) * dstSize) / srcSize;
    const int blockW = dstXEnd - dstXStart;
    if (blockW <= 0) continue;
    for (int sly = 0; sly < srcSize; ++sly) {
      // Logical (slx, sly) → native (sly, srcSize-1-slx). Bytes are row-major
      // in native: bitPos = ny * srcSize + nx.
      const int bitPos = (srcSize - 1 - slx) * srcSize + sly;
      const uint8_t byte = iconData[bitPos / 8];
      const bool isBlack = !((byte >> (7 - (bitPos % 8))) & 1);
      if (!isBlack) continue;
      const int dstYStart = (sly * dstSize) / srcSize;
      const int dstYEnd = ((sly + 1) * dstSize) / srcSize;
      const int blockH = dstYEnd - dstYStart;
      if (blockH > 0) {
        renderer.fillRect(dstX + dstXStart, dstY + dstYStart, blockW, blockH, true);
      }
    }
  }
}

float clampProgressPercent(const float progress) { return std::clamp(progress, 0.0f, 100.0f); }

bool hasAnyBookStats(const BookReadingStats& stats) {
  return stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 || stats.isCompleted;
}

bool hasAnyGlobalStats(const GlobalReadingStats& stats) {
  return stats.totalSessions > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 ||
         stats.completedBooks > 0;
}

float loadEpubProgressPercent(const RecentBook& book) {
  Epub epub(book.path, "/.crosspoint");
  if (!epub.load(false, true)) {
    return -1.0f;
  }

  FsFile file;
  if (!Storage.openFileForRead("HOME", epub.getCachePath() + "/progress.bin", file)) {
    return -1.0f;
  }

  uint8_t data[6];
  const int bytesRead = file.read(data, sizeof(data));
  file.close();
  if (bytesRead != 6) {
    return -1.0f;
  }

  const int spineIndex = data[0] | (data[1] << 8);
  const int currentPage = data[2] | (data[3] << 8);
  const int pageCount = data[4] | (data[5] << 8);
  if (pageCount <= 0) {
    return 0.0f;
  }

  const float chapterProgress = static_cast<float>(currentPage + 1) / static_cast<float>(pageCount);
  return clampProgressPercent(epub.calculateProgress(spineIndex, chapterProgress) * 100.0f);
}

float loadXtcProgressPercent(const RecentBook& book) {
  Xtc xtc(book.path, "/.crosspoint");
  if (!xtc.load()) {
    return -1.0f;
  }

  FsFile file;
  if (!Storage.openFileForRead("HOME", xtc.getCachePath() + "/progress.bin", file)) {
    return -1.0f;
  }

  uint8_t data[4];
  const int bytesRead = file.read(data, sizeof(data));
  file.close();
  if (bytesRead != 4) {
    return -1.0f;
  }

  const uint32_t currentPage = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                               (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
  return clampProgressPercent(static_cast<float>(xtc.calculateProgress(currentPage)));
}

float loadTxtProgressPercent(const RecentBook& book) {
  Txt txt(book.path, "/.crosspoint");
  if (!txt.load()) {
    return -1.0f;
  }

  FsFile progressFile;
  if (!Storage.openFileForRead("HOME", txt.getCachePath() + "/progress.bin", progressFile)) {
    return -1.0f;
  }

  uint8_t progressData[4];
  const int progressBytes = progressFile.read(progressData, sizeof(progressData));
  progressFile.close();
  if (progressBytes != 4) {
    return -1.0f;
  }

  const uint32_t currentPage = static_cast<uint32_t>(progressData[0]) | (static_cast<uint32_t>(progressData[1]) << 8);

  FsFile indexFile;
  if (!Storage.openFileForRead("HOME", txt.getCachePath() + "/index.bin", indexFile)) {
    return -1.0f;
  }

  uint32_t magic = 0;
  serialization::readPod(indexFile, magic);
  uint8_t version = 0;
  serialization::readPod(indexFile, version);
  uint32_t fileSize = 0;
  serialization::readPod(indexFile, fileSize);
  int32_t cachedWidth = 0;
  serialization::readPod(indexFile, cachedWidth);
  int32_t cachedLines = 0;
  serialization::readPod(indexFile, cachedLines);
  int32_t fontId = 0;
  serialization::readPod(indexFile, fontId);
  int32_t margin = 0;
  serialization::readPod(indexFile, margin);
  uint8_t alignment = 0;
  serialization::readPod(indexFile, alignment);
  uint32_t totalPages = 0;
  serialization::readPod(indexFile, totalPages);
  indexFile.close();
  (void)cachedWidth;
  (void)cachedLines;
  (void)fontId;
  (void)margin;
  (void)alignment;

  if (magic != TXT_CACHE_MAGIC || version != TXT_CACHE_VERSION || fileSize != txt.getFileSize() || totalPages == 0) {
    return -1.0f;
  }

  return clampProgressPercent((static_cast<float>(currentPage + 1) / static_cast<float>(totalPages)) * 100.0f);
}

float loadRecentBookProgressPercent(const RecentBook& book) {
  if (FsHelpers::hasEpubExtension(book.path)) {
    return loadEpubProgressPercent(book);
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    return loadXtcProgressPercent(book);
  }
  if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    return loadTxtProgressPercent(book);
  }
  return -1.0f;
}

// Per-book stats live at /.crosspoint/{prefix}_{hash}/stats.bin where the
// prefix tracks the format of the book file. Same dispatch as the progress
// loader above so we resolve the right cache directory per book.
std::string statsCachePathForBook(const std::string& bookPath) {
  const char* prefix = nullptr;
  if (FsHelpers::hasEpubExtension(bookPath)) prefix = "epub_";
  else if (FsHelpers::hasXtcExtension(bookPath)) prefix = "xtc_";
  else if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) prefix = "txt_";
  if (prefix == nullptr) return {};
  return "/.crosspoint/" + std::string(prefix) + std::to_string(std::hash<std::string>{}(bookPath));
}
}  // namespace

int HomeActivity::getMenuItemCount() const {
  // The home strip is a fixed 6-icon horizontal row (Recent, Stats,
  // FileTransfer, Browse, Bookmarks, Settings). The "menu count" used by
  // loop() is the total navigable count (recent books + the 6 menu items).
  int count = 6;
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  // ESP32-C3 has ~380 KB usable RAM and no PSRAM. Sequential thumb generation
  // (EPUB load + JPEG decode + BMP write) fragments the heap; once free heap
  // drops below this floor the next decode cannot find a contiguous block and
  // crashes. Bail out early instead — the home screen renders with whatever
  // covers loaded so far rather than bricking. See "more than 5 books bricks"
  // bug, May 2026.
  constexpr size_t MIN_FREE_HEAP_FOR_THUMB = 80 * 1024;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    // Skip stale entries: recent.bin may reference a book that was deleted
    // from the SD card. Opening a missing path crashes inside Epub::load.
    if (!Storage.exists(book.path.c_str())) {
      progress++;
      continue;
    }
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // Heap-floor guard before each fresh thumb gen. If we're already low,
        // stop the loop entirely so the remaining cards render as fallbacks
        // instead of risking an OOM mid-decode.
        const uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < MIN_FREE_HEAP_FOR_THUMB) {
          LOG_ERR("HOME", "Skipping remaining thumb gen (free heap %u < %u)", freeHeap,
                  static_cast<unsigned>(MIN_FREE_HEAP_FOR_THUMB));
          break;
        }
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  // Check if any books have bookmarks (directory scan only, no file parsing)
  hasBookmarks = BookmarkStore::hasAnyBookmarks();

  selectorIndex = 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  // Load reading stats for the most recent EPUB book so they can be shown on the home card.
  currentBookStats = BookReadingStats{};
  currentBookProgressPercent = -1.0f;
  if (!recentBooks.empty() && FsHelpers::hasEpubExtension(recentBooks[0].path)) {
    const std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(recentBooks[0].path));
    currentBookStats = BookReadingStats::load(cachePath);
  }
  if (!recentBooks.empty()) {
    currentBookProgressPercent = loadRecentBookProgressPercent(recentBooks[0]);
  }

  // Eagerly load per-book stats for the carousel covers. Each load is a
  // 12-byte file; with homeRecentBooksCount capped at 5 this is negligible
  // and cheaper than re-reading on every carousel scroll.
  recentBookStats.clear();
  recentBookStats.reserve(recentBooks.size());
  recentBookProgress.clear();
  recentBookProgress.reserve(recentBooks.size());
  for (const auto& book : recentBooks) {
    BookReadingStats stats;
    const std::string path = statsCachePathForBook(book.path);
    if (!path.empty()) {
      stats = BookReadingStats::load(path);
    }
    recentBookStats.push_back(stats);
    // Per-book progress: more expensive (opens the EPUB to resolve the cache
    // path and run calculateProgress), but homeRecentBooksCount is capped at
    // 5, so this is a few hundred ms one-time cost on home enter — small
    // compared to the cover thumbnail generation that happens here too.
    recentBookProgress.push_back(loadRecentBookProgressPercent(book));
  }
  globalStats = GlobalReadingStats::load();
  hasReadingStats = hasAnyBookStats(currentBookStats) || hasAnyGlobalStats(globalStats);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int recentCount = static_cast<int>(recentBooks.size());
  const bool inCarousel = static_cast<int>(selectorIndex) < recentCount;

  // X3 layout: bottom rocker (Left/Right) cycles through the horizontal menu
  // strip; side rocker (Up/Down) navigates the carousel. Pressing Left/Right
  // from inside the carousel jumps to the menu (first or last icon); once in
  // the menu, Left/Right wrap within the 6 icons. Up/Down brings the user
  // back to the centered book.
  constexpr int kHomeMenuCount = 6;
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (inCarousel) {
      lastBookIndex = selectorIndex;
      selectorIndex = recentCount;  // first menu icon
    } else {
      const int menuIdx = static_cast<int>(selectorIndex) - recentCount;
      selectorIndex = recentCount + (menuIdx + 1) % kHomeMenuCount;
    }
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (inCarousel) {
      lastBookIndex = selectorIndex;
      selectorIndex = recentCount + kHomeMenuCount - 1;  // last menu icon
    } else {
      const int menuIdx = static_cast<int>(selectorIndex) - recentCount;
      selectorIndex = recentCount + (menuIdx - 1 + kHomeMenuCount) % kHomeMenuCount;
    }
    requestUpdate();
  }

  // Side rocker (Up/Down) → carousel navigation. From the menu, the first
  // press drops back to the last book the user was on. Inside the carousel
  // it wraps end-to-end.
  //
  // The cover-buffer cache is invalidated only on the "scroll within
  // carousel" branch — menu↔carousel transitions keep the same centered
  // book (lastBookIndex), so the cached cover + chrome stay valid and the
  // next render skips its SD I/O. This is the main lag fix.
  if (recentCount > 0 && mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (!inCarousel) {
      selectorIndex = lastBookIndex < recentCount ? lastBookIndex : 0;
    } else {
      selectorIndex = (selectorIndex + 1) % recentCount;
      coverRendered = false;
      coverBufferStored = false;
    }
    requestUpdate();
  }
  if (recentCount > 0 && mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (!inCarousel) {
      selectorIndex = lastBookIndex < recentCount ? lastBookIndex : 0;
    } else {
      selectorIndex = (selectorIndex == 0) ? recentCount - 1 : selectorIndex - 1;
      coverRendered = false;
      coverBufferStored = false;
    }
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Indices match the kHomeMenuItems array in render(): Recent / Stats /
    // FileTransfer / Browse / Bookmarks / Settings.
    const int menuIdx = static_cast<int>(selectorIndex) - static_cast<int>(recentBooks.size());
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
    } else {
      switch (menuIdx) {
        case 0: onRecentsOpen(); break;
        case 1: onReadingStatsOpen(); break;
        case 2: onFileTransferOpen(); break;
        case 3: onFileBrowserOpen(); break;
        case 4: onBookmarksOpen(); break;
        case 5: onSettingsOpen(); break;
        default: break;
      }
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Home-page-only battery percentage under the battery top bar, right-aligned
  // at the same screenMargin inset the bar uses. The left/center status texts
  // we briefly tried were too cluttered, so this stays a solo element.
  {
    const uint16_t batteryPct = std::min<uint16_t>(powerManager.getBatteryPercentage(), 100);
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%u%%", static_cast<unsigned>(batteryPct));
    int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
    renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                     &orientedMarginLeft);
    const int rightInset = orientedMarginRight + SETTINGS.screenMargin;
    const int pctWidth = renderer.getTextWidth(SMALL_FONT_ID, pctBuf);
    const int pctY = 18;  // battery bar bottom at y=14; 4 px gap below it
    renderer.drawText(SMALL_FONT_ID, pageWidth - rightInset - pctWidth, pctY, pctBuf, true);
  }

  // When the user has navigated up/down into the menu, selectorIndex points
  // at a menu row (>= recentCount). The carousel's "no selection" branch
  // would otherwise pin to book 0 (most recent), losing the user's place.
  // Encode the preferred center as (recentCount + lastBookIndex) so themes
  // that scroll through covers (LyraFlowTheme) can keep that book centered;
  // single-cover themes still see "selectorIndex != 0" → no selection drawn,
  // unchanged behavior.
  const int recentCountInt = static_cast<int>(recentBooks.size());
  const bool inMenuForCarousel = static_cast<int>(selectorIndex) >= recentCountInt;
  const int carouselDisplayIndex =
      (inMenuForCarousel && lastBookIndex >= 0 && lastBookIndex < recentCountInt)
          ? recentCountInt + lastBookIndex
          : static_cast<int>(selectorIndex);
  // Stats for the book currently centered in the carousel — Flow theme uses
  // this to render an "Xh Ym" indicator under the cover. Resolve the centered
  // index the same way carouselDisplayIndex does, then index the per-book
  // vector populated in onEnter().
  const int centeredBookIdx = inMenuForCarousel
                                  ? (lastBookIndex >= 0 && lastBookIndex < recentCountInt ? lastBookIndex : 0)
                                  : static_cast<int>(selectorIndex);
  const BookReadingStats* centeredBookStats =
      (centeredBookIdx >= 0 && centeredBookIdx < static_cast<int>(recentBookStats.size()))
          ? &recentBookStats[centeredBookIdx]
          : nullptr;
  const float centeredBookProgress =
      (centeredBookIdx >= 0 && centeredBookIdx < static_cast<int>(recentBookProgress.size()))
          ? recentBookProgress[centeredBookIdx]
          : -1.0f;
  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, carouselDisplayIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this), centeredBookStats,
                          centeredBookProgress);

  // Horizontal icon strip — 6 fixed items, drawn just above the button hints.
  // Selected icon gets a light-gray rounded cell. Cycle order matches the
  // dispatch in loop() — keep them in sync. The bookmark item has a null
  // icon pointer because we draw a 5-point ribbon polygon for it instead of
  // blitting a stored bitmap (matches the bookmark glyph used elsewhere).
  struct HomeMenuItem {
    const uint8_t* icon;  // nullptr for special items rendered as polygons
    bool isBookmark;
    const char* label;
  };
  const HomeMenuItem kHomeMenuItems[6] = {
      {RecentIcon, false, tr(STR_MENU_RECENT_BOOKS)}, {ChartIcon, false, tr(STR_READING_STATS)},
      {TransferIcon, false, tr(STR_FILE_TRANSFER)},   {FolderIcon, false, tr(STR_BROWSE_FILES)},
      {nullptr, true, tr(STR_BOOKMARKS)},             {Settings2Icon, false, tr(STR_SETTINGS_TITLE)},
  };
  constexpr int kHomeMenuCount = 6;
  constexpr int kIconSrcSize = 32;   // raw bitmap dimensions (assets are 32×32)
  constexpr int iconSize = 40;       // visual size; drawIconScaled resamples 32→40
  constexpr int iconCellSize = 56;
  constexpr int sideMargin = 32;

  const int hintsTopY = pageHeight - metrics.buttonHintsHeight;
  const int iconCellBottomY = hintsTopY - 16;  // 16 px gap above the hints row
  const int iconCellTopY = iconCellBottomY - iconCellSize;
  const int totalIconArea = pageWidth - 2 * sideMargin;
  const int iconPitch = totalIconArea / kHomeMenuCount;

  const int menuSelectedIndex =
      (selectorIndex >= recentBooks.size()) ? static_cast<int>(selectorIndex - recentBooks.size()) : -1;

  for (int i = 0; i < kHomeMenuCount; ++i) {
    const int cellCenterX = sideMargin + iconPitch / 2 + i * iconPitch;
    const int cellX = cellCenterX - iconCellSize / 2;
    const int iconX = cellCenterX - iconSize / 2;
    const int iconY = iconCellTopY + (iconCellSize - iconSize) / 2;
    if (i == menuSelectedIndex) {
      renderer.fillRoundedRect(cellX, iconCellTopY, iconCellSize, iconCellSize, 6, Color::LightGray);
    }
    if (kHomeMenuItems[i].isBookmark) {
      // 5-point bookmark ribbon (top-left → top-right → bottom-right → notch
      // tip → bottom-left). Geometry mirrors LyraTheme's tile-row bookmark
      // glyph, scaled to the icon cell so 6 icons share the same visual
      // weight on the strip.
      const int ribbonW = (iconSize * 16) / 22;
      const int ribbonH = iconSize;
      const int notchH = (iconSize * 6) / 22;
      const int rIconX = iconX + (iconSize - ribbonW) / 2;
      const int rIconY = iconY;
      const int notchTipX = rIconX + ribbonW / 2;
      const int polyX[5] = {rIconX, rIconX + ribbonW, rIconX + ribbonW, notchTipX, rIconX};
      const int polyY[5] = {rIconY, rIconY, rIconY + ribbonH, rIconY + ribbonH - notchH, rIconY + ribbonH};
      renderer.fillPolygon(polyX, polyY, 5, true);
    } else if (kHomeMenuItems[i].icon != nullptr) {
      drawIconScaled(renderer, kHomeMenuItems[i].icon, kIconSrcSize, iconX, iconY, iconSize);
    }
  }

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onReadingStatsOpen() {
  // Replace home with stats so backing out lands on a fresh home with
  // selectorIndex reset to 0 — same behavior as Browse Files / Recent
  // Books / Settings, instead of the stack-preserved variant we had
  // before. The reader's own stats menu still uses startActivityForResult
  // (defined in EpubReaderActivity), so back from there returns to the
  // reader as expected.
  if (recentBooks.empty()) return;
  activityManager.replaceActivity(std::make_unique<BookStatsActivity>(
      renderer, mappedInput, recentBooks[0].path, recentBooks[0].title, recentBooks[0].coverBmpPath, currentBookStats,
      globalStats, /*backToHome=*/true));
}

void HomeActivity::onBookmarksOpen() {
  startActivityForResult(std::make_unique<BookmarksHomeActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}
