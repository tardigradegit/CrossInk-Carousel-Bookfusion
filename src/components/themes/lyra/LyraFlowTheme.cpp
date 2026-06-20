#include "LyraFlowTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/chart.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {
// X3 carousel: tighter overlap (32 px instead of the X4 base 16) and scaled
// up so the L-far cover still sits at ~29 px from the screen edge — the
// stack reads more compact even though every cover is bigger.
constexpr int centerCoverWidth = 270;
constexpr int centerCoverHeight = 392;
constexpr int sideCoverWidth = 82;
constexpr int sideInnerHeight = 353;
constexpr int sideOuterHeight = 314;
constexpr int bookCornerRadius = 6;

// Side-cover x-positions chosen so each adjacent pair (L-far/L-near,
// L-near/center, R-near/center, R-far/R-near) overlaps by 32 px and the L-far
// cover starts at x=29 from the left edge. The right-side positions are
// mirrored from the right screen edge at render time so they stay symmetric
// across X3 (528 px wide) and X4 (480 px wide).
constexpr int sideXLeftFar = 29;
constexpr int sideXLeftNear = 79;
constexpr int sideXRightEdgeFar = 29;
constexpr int sideXRightEdgeNear = 79;

// Menu visuals — kept in sync with LyraTheme's anonymous-namespace constants
// so the Flow override looks identical to the parent's button menu.
constexpr int menuTileCornerRadius = 6;
constexpr int menuTilePadding = 8;
constexpr int menuIconSize = 32;

// Same lookup as LyraTheme's iconForName(icon, 32). Duplicated here because
// that helper is file-local to LyraTheme.cpp.
const uint8_t* lyraFlowMenuIcon(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Chart:
      return ChartIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    default:
      return nullptr;
  }
}

// Erase pixels outside a rounded-corner mask so a rectangular bitmap blit
// looks like a rounded book cover. Same trick as the reference FlowTheme.
void cutRoundedCorners(const GfxRenderer& renderer, int x, int y, int w, int h, int r) {
  const int rSq = r * r;
  for (int dy = 0; dy < r; dy++) {
    for (int dx = 0; dx < r; dx++) {
      const int distSq = (r - dx) * (r - dx) + (r - dy) * (r - dy);
      if (distSq > rSq) {
        renderer.drawPixel(x + dx, y + dy, false);
        renderer.drawPixel(x + w - 1 - dx, y + dy, false);
        renderer.drawPixel(x + w - 1 - dx, y + h - 1 - dy, false);
        renderer.drawPixel(x + dx, y + h - 1 - dy, false);
      }
    }
  }
}
}  // namespace

void LyraFlowTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                        int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                        bool& bufferRestored, const std::function<bool()>& storeCoverBuffer,
                                        const BookReadingStats* stats, float progressPercent) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int pageWidth = renderer.getScreenWidth();
  const int centerX = pageWidth / 2;
  // Cover sits flush to the top of the rect (with a small breathing margin);
  // title + author now live BELOW the cover, so we no longer need the
  // text-driven clamp that used to push the cover down to clear text above it.
  // Cover top offset within the rect. Bumped 8 → 48 to push the carousel
  // lower and shrink the leftover vertical room around the title/author block
  // (which is centered inside that leftover space).
  constexpr int kCoverTopOffset = 48;
  const int titleLh = renderer.getLineHeight(UI_12_FONT_ID);
  const int authorLh = renderer.getLineHeight(UI_10_FONT_ID);
  const int count = static_cast<int>(recentBooks.size());

  // selectorIndex >= count means HomeActivity has navigated past the books and
  // is highlighting a menu item; in that case we keep the carousel visible but
  // drop the selection border. HomeActivity may encode the preferred center as
  // (count + lastBookIndex) so the carousel keeps the user's place when they
  // pop into the menu. Decode if in range, otherwise fall back to book 0.
  const bool hasSelection = (selectorIndex >= 0 && selectorIndex < count);
  int curIdx = 0;
  if (hasSelection) {
    curIdx = selectorIndex;
  } else {
    const int decoded = selectorIndex - count;
    if (decoded >= 0 && decoded < count) curIdx = decoded;
  }

  // Cover top is fixed near the top of the rect — title+author live beneath
  // the cover now, so there's no text above to dodge.
  const bool hasAuthorLine = !recentBooks[curIdx].author.empty();
  const int centerY = rect.y + kCoverTopOffset;

  // ────────────────────────────────────────────────────────────────────────
  // Buffer gate: skip the SD-heavy cover loading + chrome drawing when the
  // previous render's framebuffer was successfully cached and the centered
  // book hasn't changed. HomeActivity invalidates `coverRendered` and
  // `coverBufferStored` whenever the user scrolls within the carousel; menu
  // navigation (Left/Right) leaves them set so this branch is taken and
  // input feels responsive.
  if (!coverRendered) {
    // --- Side covers (perspective-projected, drawn outside-in so the center
    //     can land cleanly on top of any near-book overlap) ---
    auto drawStackedCover = [&](int idx, bool isLeft, bool isFar) {
      const int hL = isLeft ? sideInnerHeight : sideOuterHeight;
      const int hR = isLeft ? sideOuterHeight : sideInnerHeight;
      const int hMax = std::max(hL, hR);
      // Right-side positions mirror the left from the screen's right edge so
      // both sides stay symmetric on X3 (528 px) and X4 (480 px).
      const int rightFarX = pageWidth - sideXRightEdgeFar - sideCoverWidth;
      const int rightNearX = pageWidth - sideXRightEdgeNear - sideCoverWidth;
      const int drawX = isLeft ? (isFar ? sideXLeftFar : sideXLeftNear) : (isFar ? rightFarX : rightNearX);
      const int drawY = centerY + (centerCoverHeight / 2) - (hMax / 2);

      const std::string coverPath = UITheme::getCoverThumbPath(recentBooks[idx].coverBmpPath, centerCoverHeight);
      bool drawn = false;
      if (!coverPath.empty()) {
        FsFile file;
        if (Storage.openFileForRead("HOME", coverPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            // drawPerspectiveBitmap is OR-style (only writes black), so any
            // white area of the cover would show through to whatever side
            // cover was drawn beneath us. Pre-clear the bbox to opaque white.
            renderer.fillRect(drawX, drawY, sideCoverWidth, hMax, false);
            renderer.drawPerspectiveBitmap(bitmap, drawX, drawY, sideCoverWidth, hL, hR);
            drawn = true;
          }
          file.close();
        }
      }
      if (!drawn) {
        // Solid-black placeholder silhouette so the carousel still has shape.
        renderer.fillRect(drawX, drawY, sideCoverWidth, hMax, true);
        return;  // outline would be invisible against solid black anyway
      }
      // 2px trapezoidal outline matching the perspective shape — keeps every
      // side book visibly framed so the center book reads as part of a row of
      // books, not a single floating cover. The trapezoid is column-centered
      // vertically inside the (sideCoverWidth × hMax) bbox.
      const int topL = (hMax - hL) / 2;
      const int topR = (hMax - hR) / 2;
      const int botL = topL + hL - 1;
      const int botR = topR + hR - 1;
      const int rightX = drawX + sideCoverWidth - 1;
      renderer.drawLine(drawX, drawY + topL, rightX, drawY + topR, 2, true);  // top edge (slanted)
      renderer.drawLine(drawX, drawY + botL, rightX, drawY + botR, 2, true);  // bottom edge (slanted)
      // Verticals use fillRect, not drawLine — drawLine ignores its thickness
      // arg for purely vertical strokes (x1 == x2), so the previous 4 px width
      // was rendering as 1 px regardless. fillRect gives explicit control.
      constexpr int verticalEdgeWidth = 2;
      renderer.fillRect(drawX, drawY + topL, verticalEdgeWidth, hL, true);                    // left edge
      renderer.fillRect(rightX - verticalEdgeWidth + 1, drawY + topR, verticalEdgeWidth, hR,  // right edge
                        true);
      // The bottom slant's perpendicular thickness leaks pixels into the two
      // rows starting just below the bbox bottom (the row at drawY + hMax
      // is part of the visible outline, so we leave it). Wipe rows hMax+1
      // and hMax+2 to catch the hangnail wherever it lands.
      renderer.fillRect(drawX, drawY + hMax + 1, sideCoverWidth, 2, false);
    };

    const int idx2 = (curIdx + count - 1) % count;  // left-near
    const int idx3 = (curIdx + count - 2) % count;  // left-far
    const int idx4 = (curIdx + 1) % count;          // right-near
    const int idx5 = (curIdx + 2) % count;          // right-far

    if (count >= 5) drawStackedCover(idx3, true, true);
    if (count >= 4) drawStackedCover(idx5, false, true);
    if (count >= 2) drawStackedCover(idx2, true, false);
    if (count >= 3) drawStackedCover(idx4, false, false);

    // --- Center cover. Peek the bitmap dimensions first so the slot, outline,
    //     and selection border match the cover's true aspect ratio (otherwise
    //     drawBitmap aspect-fits but our 220×320 chrome leaves a white sliver
    //     for narrower covers, e.g. 1720×2600 which is taller than 220:320). ---
    int actualCoverWidth = centerCoverWidth;
    int actualCoverHeight = centerCoverHeight;
    const std::string cp = UITheme::getCoverThumbPath(recentBooks[curIdx].coverBmpPath, centerCoverHeight);
    FsFile cf;
    const bool centerOpened = !cp.empty() && Storage.openFileForRead("HOME", cp, cf);
    Bitmap centerBitmap(cf);
    bool centerParsed = false;
    if (centerOpened) {
      if (centerBitmap.parseHeaders() == BmpReaderError::Ok && centerBitmap.getWidth() > 0 &&
          centerBitmap.getHeight() > 0) {
        const int srcW = centerBitmap.getWidth();
        const int srcH = centerBitmap.getHeight();
        const float fitScale = std::min(static_cast<float>(centerCoverWidth) / static_cast<float>(srcW),
                                        static_cast<float>(centerCoverHeight) / static_cast<float>(srcH));
        actualCoverWidth = std::min(centerCoverWidth, static_cast<int>(std::round(srcW * fitScale)));
        actualCoverHeight = std::min(centerCoverHeight, static_cast<int>(std::round(srcH * fitScale)));
        centerParsed = true;
      }
    }

    const int cX = centerX - actualCoverWidth / 2;
    // Vertical-center within the original 320-tall slot in case a cover is wider
    // than tall (very rare in practice).
    const int actualY = centerY + (centerCoverHeight - actualCoverHeight) / 2;

    // Clear behind it so any side-cover overlap doesn't bleed through.
    renderer.fillRect(cX, actualY, actualCoverWidth, actualCoverHeight, false);

    if (centerParsed) {
      renderer.drawBitmap(centerBitmap, cX, actualY, actualCoverWidth, actualCoverHeight);
      cutRoundedCorners(renderer, cX, actualY, actualCoverWidth, actualCoverHeight, bookCornerRadius);
    } else {
      // Placeholder: black lower-2/3 with the cover icon, matches reference fallback.
      renderer.fillRoundedRect(cX, actualY + actualCoverHeight / 3, actualCoverWidth, 2 * actualCoverHeight / 3,
                               bookCornerRadius, false, false, true, true, Color::Black);
      renderer.drawIcon(CoverIcon, cX + actualCoverWidth / 2 - 16, actualY + actualCoverHeight / 2 - 16, 32, 32);
    }
    renderer.drawRoundedRect(cX, actualY, actualCoverWidth, actualCoverHeight, 2, bookCornerRadius, true);

    if (centerOpened) cf.close();

    // Cache positions so the selection-border-only path below can redraw the
    // border on subsequent frames without re-running the SD load above.
    cachedCenterCoverX = cX;
    cachedCenterCoverY = actualY;
    cachedActualCoverWidth = actualCoverWidth;
    cachedActualCoverHeight = actualCoverHeight;

    // --- Title above the center cover (filename, no extension) ---
    std::string filename = recentBooks[curIdx].title.empty() ? recentBooks[curIdx].path : recentBooks[curIdx].title;
    if (recentBooks[curIdx].title.empty()) {
      const size_t lastSlash = filename.find_last_of('/');
      if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
      const size_t lastDot = filename.find_last_of('.');
      if (lastDot != std::string::npos && lastDot > 0) filename.resize(lastDot);
    }

    // Tight progress bar hugging the bottom of the cover (4 px gap). Same
    // 1-px-track + 3-px-fill family as the reader / battery bars. Width and
    // left edge follow the actual rendered cover (cX/actualCoverWidth) instead
    // of the slot dims, so the bar lines up flush with the unselected cover
    // edges — including books whose aspect makes the rendered cover narrower
    // than the 270-px slot.
    const int progressBarTopY = centerY + centerCoverHeight + 8;
    constexpr int progressBarVisualHeight = 3;
    if (progressPercent >= 0.0f) {
      const float clamped = std::clamp(progressPercent, 0.0f, 100.0f);
      const int barLeftX = cX;
      const int barW = actualCoverWidth;
      const int fillW = static_cast<int>((barW * clamped) / 100.0f);
      // Track is the middle row of the 3-px fill band (was the bottom row) so
      // the unfilled track and the thicker filled portion share a horizontal
      // centerline. Visually: the thin line sits halfway through the thick
      // line's height instead of at its bottom edge.
      renderer.fillRect(barLeftX, progressBarTopY + 1, barW, 1, true);
      if (fillW > 0) {
        renderer.fillRect(barLeftX, progressBarTopY, fillW, progressBarVisualHeight, true);
      }
    }

    // YouTube/iPod-style time indicator under the progress bar. Elapsed time
    // hugs the bar's left edge, projected total hugs the right edge, so the
    // cover + bar + times cluster reads as one card. Both align to the actual
    // rendered cover (cX, actualCoverWidth) rather than the slot dims, so they
    // shift with narrower covers like the bar does.
    //
    // Edge cases (finished books / no useful projection) collapse to a single
    // centered time. Total page count isn't cheaply available in this codebase
    // (Epub::getBookSize returns bytes, not pages, and per-chapter page counts
    // depend on the user's font setup), so we don't try to extrapolate via
    // global pages/min for the unstarted case — single time is cleaner anyway.
    const int timeReadFontLh = renderer.getLineHeight(SMALL_FONT_ID);
    // Bring the time row up so the gap between bar bottom and text top matches
    // the 4-px gap between the unselected cover bottom and the bar top.
    // drawText's `y` is bbox top but the visible glyph starts ~2 px below it
    // (top-side leading), so +2 here gives a ~4-px optical gap.
    const int timeReadY = progressBarTopY + progressBarVisualHeight + 6;
    {
      const uint32_t elapsedSecs = (stats != nullptr) ? stats->totalReadingSeconds : 0;
      const bool isCompleted = (stats != nullptr && stats->isCompleted);
      const bool inReadFolder = recentBooks[curIdx].path.find("/Read/") != std::string::npos;
      const bool finished = isCompleted || inReadFolder || (progressPercent >= 99.5f);

      auto formatHMM = [](uint32_t seconds, char* buf, size_t len) {
        const uint32_t hours = seconds / 3600;
        const uint32_t minutes = (seconds % 3600) / 60;
        snprintf(buf, len, "%u:%02u", static_cast<unsigned>(hours), static_cast<unsigned>(minutes));
      };

      char elapsedBuf[12];
      formatHMM(elapsedSecs, elapsedBuf, sizeof(elapsedBuf));

      if (finished || elapsedSecs == 0 || progressPercent < 0.1f) {
        // Single centered time — no useful right-side projection.
        const int w = renderer.getTextWidth(SMALL_FONT_ID, elapsedBuf);
        renderer.drawText(SMALL_FONT_ID, centerX - w / 2, timeReadY, elapsedBuf, true);
      } else {
        const float projectedSecsF = static_cast<float>(elapsedSecs) * 100.0f / progressPercent;
        const uint32_t projectedSecs = static_cast<uint32_t>(projectedSecsF + 0.5f);
        char projectedBuf[12];
        formatHMM(projectedSecs, projectedBuf, sizeof(projectedBuf));
        const int projW = renderer.getTextWidth(SMALL_FONT_ID, projectedBuf);
        const int projectedLeftEdge = cX + actualCoverWidth - projW;
        // Elapsed on the bar's left edge, projected right-aligned with the bar's
        // right edge.
        renderer.drawText(SMALL_FONT_ID, cX, timeReadY, elapsedBuf, true);
        renderer.drawText(SMALL_FONT_ID, projectedLeftEdge, timeReadY, projectedBuf, true);
      }
    }

    // Title + author centered in the leftover vertical room between the time
    // line and the rect bottom (which is laid out to align with the menu strip
    // top — see LyraFlowMetrics::homeCoverTileHeight).
    const int textAreaTop = timeReadY + timeReadFontLh + 1;
    const int textAreaBottom = rect.y + rect.height;
    const int textAreaHeight = std::max(0, textAreaBottom - textAreaTop);
    const int titleBlockHeight = titleLh + (hasAuthorLine ? (1 + authorLh) : 0);
    const int titleTopY = textAreaTop + std::max(0, (textAreaHeight - titleBlockHeight) / 2);

    const std::string truncatedTitle =
        renderer.truncatedText(UI_12_FONT_ID, filename.c_str(), pageWidth - 40, EpdFontFamily::BOLD);
    const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, truncatedTitle.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, centerX - titleWidth / 2, titleTopY, truncatedTitle.c_str(), true,
                      EpdFontFamily::BOLD);

    if (hasAuthorLine) {
      const int authorY = titleTopY + titleLh + 1;
      const std::string truncatedAuthor =
          renderer.truncatedText(UI_10_FONT_ID, recentBooks[curIdx].author.c_str(), pageWidth - 40);
      const int authorWidth = renderer.getTextWidth(UI_10_FONT_ID, truncatedAuthor.c_str());
      renderer.drawText(UI_10_FONT_ID, centerX - authorWidth / 2, authorY, truncatedAuthor.c_str(), true);
    }

    // Snapshot the cover + chrome (everything we just drew, minus the
    // selection border which is drawn per-frame below). The next render can
    // restore this in one memcpy and skip every SD I/O above.
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }  // end of `if (!coverRendered)` gate

  // Selection border drawn EVERY frame outside the gate so the rectangle can
  // toggle (carousel ↔ menu) without invalidating the cached buffer.
  if (hasSelection && cachedActualCoverWidth > 0) {
    renderer.drawRoundedRect(cachedCenterCoverX - 2, cachedCenterCoverY - 2, cachedActualCoverWidth + 4,
                             cachedActualCoverHeight + 4, 4, bookCornerRadius + 2, true);
  }
}

void LyraFlowTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                   const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>& rowIcon) const {
  const auto& menuMetrics = UITheme::getInstance().getMetrics();
  const int rowStep = menuMetrics.menuRowHeight + menuMetrics.menuSpacing;
  // Reserve a thin strip at the bottom for page-indicator dots. Reserving
  // unconditionally keeps tile geometry stable whether dots are visible or not.
  constexpr int dotSize = 10;
  constexpr int dotSpacing = 8;
  constexpr int dotStripHeight = 18;
  const int usableHeight = std::max(0, rect.height - dotStripHeight);
  const int pageItems = std::max(1, usableHeight / rowStep);
  const int safeSelectedIndex = std::max(0, selectedIndex);

  // Two-anchor pagination: page 1 = items [0..pageItems), page 2 =
  // last `pageItems` items. The pages overlap; we resolve which one
  // to render via a sticky bit so the cursor "pulls" the visible
  // window with it asymmetrically — page 2 holds while the cursor
  // scrolls up through the overlap, and only flips back to page 1
  // when the cursor crosses page 2's top boundary.
  const bool needsPaging = buttonCount > pageItems;
  const int page2StartIndex = std::max(0, buttonCount - pageItems);
  if (!needsPaging) {
    stickyMenuPage2 = false;
  } else if (safeSelectedIndex >= pageItems) {
    // Cursor is in page 2's exclusive zone — we're definitely on page 2.
    stickyMenuPage2 = true;
  } else if (safeSelectedIndex < page2StartIndex) {
    // Cursor crossed page 2's top boundary going up — back to page 1.
    stickyMenuPage2 = false;
  }
  // Else: cursor in the overlap zone, keep whichever page we were on.
  const bool onPage2 = needsPaging && stickyMenuPage2;
  const int pageStartIndex = onPage2 ? page2StartIndex : 0;
  const int totalPages = needsPaging ? 2 : 1;
  const int currentPage = onPage2 ? 1 : 0;

  for (int i = pageStartIndex; i < buttonCount && i < pageStartIndex + pageItems; ++i) {
    const int displayIndex = i - pageStartIndex;
    const int tileWidth = rect.width - menuMetrics.contentSidePadding * 2;
    const Rect tileRect{rect.x + menuMetrics.contentSidePadding, rect.y + displayIndex * rowStep, tileWidth,
                        menuMetrics.menuRowHeight};

    const bool selected = (i == selectedIndex);
    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, menuTileCornerRadius,
                               Color::LightGray);
    }

    const std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 16;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (menuMetrics.menuRowHeight - lineHeight) / 2;

    if (rowIcon != nullptr) {
      const UIIcon icon = rowIcon(i);
      if (icon == UIIcon::BookmarkIcon) {
        // Match the status-bar bookmark ribbon shape (LyraTheme parity).
        const int ribbonWidth = 16;
        const int ribbonHeight = 22;
        const int notchSize = 6;
        const int iconX = textX + (menuIconSize - ribbonWidth) / 2;
        const int iconY = textY + 4;
        const int centerX = iconX + ribbonWidth / 2;
        const int polyX[5] = {iconX, iconX + ribbonWidth, iconX + ribbonWidth, centerX, iconX};
        const int polyY[5] = {iconY, iconY, iconY + ribbonHeight, iconY + ribbonHeight - notchSize,
                              iconY + ribbonHeight};
        renderer.fillPolygon(polyX, polyY, 5, true);
        textX += menuIconSize + menuTilePadding + 2;
      } else {
        const uint8_t* iconBitmap = lyraFlowMenuIcon(icon);
        if (iconBitmap != nullptr) {
          renderer.drawIcon(iconBitmap, textX, textY + 3, menuIconSize, menuIconSize);
          textX += menuIconSize + menuTilePadding + 2;
        }
      }
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }

  // Page-indicator dots — pattern lifted from RecentBooksGridActivity::render.
  // Anchor at the same vertical offset above the button hints as Recent Books
  // does (rect.y + rect.height == pageHeight - buttonHintsHeight for the home
  // menu rect, so this formula resolves to the same Y as Recent Books's
  // pageHeight - buttonHintsHeight - verticalSpacing - 4).
  if (totalPages > 1) {
    const int totalDotWidth = totalPages * dotSize + (totalPages - 1) * dotSpacing;
    const int dotsStartX = rect.x + (rect.width - totalDotWidth) / 2;
    const int dotY = rect.y + rect.height - menuMetrics.verticalSpacing - 4;
    constexpr int dotRadius = dotSize / 2;  // 5 → fully-circular bullet on 10x10
    for (int p = 0; p < totalPages; ++p) {
      const int dx = dotsStartX + p * (dotSize + dotSpacing);
      if (p == currentPage) {
        renderer.fillRoundedRect(dx, dotY, dotSize, dotSize, dotRadius, Color::Black);
      } else {
        renderer.drawRoundedRect(dx, dotY, dotSize, dotSize, 1, dotRadius, true);
      }
    }
  }
}
