#pragma once
#include <I18n.h>

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

// Lua-fork-style grid view of recent books. 3x3 covers per page, paged when
// there are more than 9 books, on-demand thumbnail generation behind a
// loading popup. Replaces the plain-list RecentBooksActivity at the dispatch
// site (ActivityManager::goToRecentBooks); the original activity file is
// kept untouched for clean upstream merges.
class RecentBooksGridActivity final : public Activity {
 public:
  static constexpr int BOOKS_PER_PAGE = 9;  // 3 cols x 3 rows
  static constexpr int MAX_BOOKS = BOOKS_PER_PAGE * 2;
  // X3-tuned slot. 136×204 keeps the 2:3 aspect that matches a typical book
  // cover (bitmaps fill the slot without padding). Sized in tandem with the
  // 14-px gridSpacing in render() so total grid width stays 3·136 + 2·14 = 436
  // — same span as the previous 140×210 + 8-px grid, just with more breathing
  // room between covers.
  static constexpr int COVER_HEIGHT = 204;
  static constexpr int COVER_WIDTH = 136;

 private:
  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  std::vector<RecentBook> recentBooks;
  // Track which page's covers we've already attempted to generate, so we
  // only run the heavy Epub/Xtc thumb path when the user pages.
  int loadedPageStart = -1;
  // Cached reading-progress for whichever book is currently selected. Reading
  // progress.bin off SD costs a real disk hit, so we only refresh when the
  // selector moves to a new book.
  size_t cachedProgressIndex = std::numeric_limits<size_t>::max();
  float cachedProgressPercent = -1.0f;

  void loadRecentBooks();
  void loadPageCovers(int pageStart);

 public:
  explicit RecentBooksGridActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooksGrid", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
