#pragma once

#include <cstddef>

#include "BookFusionSyncClient.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Browse and download books from the user's BookFusion library.
 *
 * Shows the user's library 20 books at a time (paginated).
 * Selecting a book fetches a pre-signed download URL, streams the EPUBh
 * to the SD card, and writes a BookFusion sidecar via BookFusionBookIdStore
 * so that progress sync works immediately after download.
 *
 * Requires a linked BookFusion account (token in BF_TOKEN_STORE).
 */
class BookFusionBrowserActivity final : public Activity {
 public:
  explicit BookFusionBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BookFusionBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum State { CATEGORY_SELECTION, WIFI_SELECTION, LOADING, BROWSING, DOWNLOADING, DOWNLOAD_COMPLETE, ERROR };

  State state = CATEGORY_SELECTION;
  ButtonNavigator buttonNavigator;

  BookFusionSearchResult searchResult;
  int selectedIndex = 0;
  int currentPage = 1;

  int selectedCategory = 0;
  int currentCategory = 0;

  // Large enough for pre-signed S3 URLs (BookFusion URLs can exceed 1450 bytes).
  char downloadUrl[2048] = {};
  char downloadTitle[64] = {};
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  bool cancelRequested = false;

  char errorMsg[128] = {};

  void onWifiSelectionComplete(bool success);
  void handleCategorySelection();
  void loadPage(int page);
  void startDownload(int bookIndex);
};
