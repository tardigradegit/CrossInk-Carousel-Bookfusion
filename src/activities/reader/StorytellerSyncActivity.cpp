#include "StorytellerSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "ProgressMapper.h"
#include "StorytellerBookIdStore.h"
#include "StorytellerSyncClient.h"
#include "StorytellerTokenStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

void wifiOff() {
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

// Case-insensitive ASCII comparison for title matching.
bool titleMatch(const char* a, const char* b) {
  while (*a && *b) {
    if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

// Convert CrossPoint reading position to a Storyteller Readium locator.
StorytellerPosition crossPointToStoryteller(const std::shared_ptr<Epub>& epub, int spineIndex, int pageNumber,
                                            int totalPages) {
  const int spineCount = epub->getSpineItemsCount();
  const float intraSpine = (totalPages > 0) ? static_cast<float>(pageNumber) / totalPages : 0.0f;
  const float totalProgression =
      (spineCount > 0) ? (spineIndex + intraSpine) / static_cast<float>(spineCount) : 0.0f;

  StorytellerPosition pos;
  pos.href = epub->getSpineItem(spineIndex).href;
  pos.totalProgression = totalProgression;
  pos.timestamp = 0;  // filled in by setProgress
  return pos;
}

// Convert a Storyteller Readium locator back to CrossPoint coordinates.
// Returns the best-effort spine index; page resolution uses totalProgression.
struct ResolvedPosition {
  int spineIndex;
  int pageNumber;
};

ResolvedPosition storytellerToCrossPoint(const std::shared_ptr<Epub>& epub, const StorytellerPosition& st,
                                         int totalPagesInSpine) {
  // Resolve spine index from href when possible; fall back to progression.
  int spineIndex = epub->resolveHrefToSpineIndex(st.href);
  if (spineIndex < 0) {
    const int spineCount = epub->getSpineItemsCount();
    spineIndex = (spineCount > 0) ? static_cast<int>(st.totalProgression * spineCount) : 0;
    spineIndex = std::max(0, std::min(spineIndex, spineCount - 1));
  }

  // Use the KOReader ProgressMapper to translate whole-book progression → page.
  KOReaderPosition koPos;
  koPos.percentage = st.totalProgression;
  koPos.xpath = "";
  const CrossPointPosition cp = ProgressMapper::toCrossPoint(epub, koPos, spineIndex, totalPagesInSpine);

  return {cp.spineIndex, cp.pageNumber};
}

}  // namespace

// ---------------------------------------------------------------------------

void StorytellerSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_DBG("STSync", "WiFi connection failed");
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  LOG_DBG("STSync", "WiFi connected, resolving book UUID");

  // Check sidecar first; if not found, fetch book list from server.
  if (StorytellerBookIdStore::loadBookUuid(epubPath.c_str(), bookUuid, sizeof(bookUuid))) {
    LOG_DBG("STSync", "UUID from sidecar: %s", bookUuid);
    performSync();
    return;
  }

  {
    RenderLock lock(*this);
    state = RESOLVING_BOOK;
    statusMessage = tr(STR_ST_RESOLVING_BOOK);
  }
  requestUpdateAndWait();
  resolveBookUuid();
}

void StorytellerSyncActivity::resolveBookUuid() {
  StorytellerBookList books;
  const auto result = StorytellerSyncClient::getBooks(books);

  if (result != StorytellerSyncClient::OK) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = StorytellerSyncClient::errorString(result);
    }
    requestUpdate(true);
    return;
  }

  const std::string epubTitle = epub->getTitle();

  for (int i = 0; i < books.count; ++i) {
    if (titleMatch(books.books[i].title, epubTitle.c_str())) {
      strlcpy(bookUuid, books.books[i].uuid, sizeof(bookUuid));
      StorytellerBookIdStore::saveBookUuid(epubPath.c_str(), bookUuid);
      LOG_DBG("STSync", "Matched book by title: uuid=%s", bookUuid);
      performSync();
      return;
    }
  }

  // No title match found.
  {
    RenderLock lock(*this);
    state = BOOK_NOT_FOUND;
  }
  requestUpdate(true);
}

void StorytellerSyncActivity::performSync() {
  {
    RenderLock lock(*this);
    state = SYNCING;
    statusMessage = tr(STR_FETCH_PROGRESS);
  }
  requestUpdateAndWait();

  const auto result = StorytellerSyncClient::getProgress(bookUuid, remotePosition);

  if (result == StorytellerSyncClient::NOT_FOUND) {
    {
      RenderLock lock(*this);
      state = NO_REMOTE_PROGRESS;
    }
    requestUpdate(true);
    return;
  }

  if (result != StorytellerSyncClient::OK) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = StorytellerSyncClient::errorString(result);
    }
    requestUpdate(true);
    return;
  }

  // Convert remote Storyteller position → CrossPoint for display.
  const auto resolved = storytellerToCrossPoint(epub, remotePosition, totalPagesInSpine);
  remoteSpineIndex = resolved.spineIndex;
  remotePage = resolved.pageNumber;
  remoteProgressPct = remotePosition.totalProgression * 100.0f;

  // Compute local progress percentage for comparison.
  const StorytellerPosition localSt = crossPointToStoryteller(epub, currentSpineIndex, currentPage, totalPagesInSpine);
  localProgressPct = localSt.totalProgression * 100.0f;

  {
    RenderLock lock(*this);
    state = SHOWING_RESULT;
    selectedOption = (localProgressPct > remoteProgressPct) ? 1 : 0;
  }
  requestUpdate(true);
}

void StorytellerSyncActivity::performUpload() {
  {
    RenderLock lock(*this);
    state = UPLOADING;
    statusMessage = tr(STR_UPLOAD_PROGRESS);
  }
  requestUpdateAndWait();

  const StorytellerPosition pos =
      crossPointToStoryteller(epub, currentSpineIndex, currentPage, totalPagesInSpine);

  const auto result = StorytellerSyncClient::setProgress(bookUuid, pos);

  wifiOff();

  if (result != StorytellerSyncClient::OK) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = StorytellerSyncClient::errorString(result);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = UPLOAD_COMPLETE;
  }
  requestUpdate(true);
}

void StorytellerSyncActivity::onEnter() {
  Activity::onEnter();

  if (!ST_TOKEN_STORE.hasToken()) {
    state = NO_TOKEN;
    requestUpdate();
    return;
  }
  if (!ST_TOKEN_STORE.hasServerUrl()) {
    state = NO_SERVER_URL;
    requestUpdate();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void StorytellerSyncActivity::onExit() {
  Activity::onExit();
  wifiOff();
}

void StorytellerSyncActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_ST_SYNC), true, EpdFontFamily::BOLD);

  if (state == NO_TOKEN) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) - 20, tr(STR_ST_NO_TOKEN_MSG), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) + 10, tr(STR_ST_SETUP_HINT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == NO_SERVER_URL) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) - 20, tr(STR_ST_NO_URL_MSG), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) + 10, tr(STR_ST_SETUP_HINT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == RESOLVING_BOOK || state == SYNCING || state == UPLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                              statusMessage.c_str(), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == BOOK_NOT_FOUND) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) - 20, tr(STR_ST_BOOK_NOT_FOUND), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) + 10, epub->getTitle().c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SHOWING_RESULT) {
    renderer.drawCenteredText(UI_10_FONT_ID, 120, tr(STR_PROGRESS_FOUND), true, EpdFontFamily::BOLD);

    const int remoteTocIndex = epub->getTocIndexForSpineIndex(remoteSpineIndex);
    const int localTocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);

    const std::string remoteChapter =
        (remoteTocIndex >= 0)
            ? epub->getTocItem(remoteTocIndex).title
            : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(remoteSpineIndex + 1));
    const std::string localChapter =
        (localTocIndex >= 0)
            ? epub->getTocItem(localTocIndex).title
            : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(currentSpineIndex + 1));

    renderer.drawText(UI_10_FONT_ID, 20, 160, tr(STR_REMOTE_LABEL), true);
    char remoteChapterStr[128];
    snprintf(remoteChapterStr, sizeof(remoteChapterStr), "  %s", remoteChapter.c_str());
    renderer.drawText(UI_10_FONT_ID, 20, 185, remoteChapterStr);
    char remotePageStr[64];
    snprintf(remotePageStr, sizeof(remotePageStr), tr(STR_PAGE_OVERALL_FORMAT), remotePage + 1, remoteProgressPct);
    renderer.drawText(UI_10_FONT_ID, 20, 210, remotePageStr);

    renderer.drawText(UI_10_FONT_ID, 20, 270, tr(STR_LOCAL_LABEL), true);
    char localChapterStr[128];
    snprintf(localChapterStr, sizeof(localChapterStr), "  %s", localChapter.c_str());
    renderer.drawText(UI_10_FONT_ID, 20, 295, localChapterStr);
    char localPageStr[64];
    snprintf(localPageStr, sizeof(localPageStr), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), currentPage + 1,
             totalPagesInSpine, localProgressPct);
    renderer.drawText(UI_10_FONT_ID, 20, 320, localPageStr);

    const int optionY = 350;
    const int optionHeight = 30;

    if (selectedOption == 0) renderer.fillRect(0, optionY - 2, pageWidth - 1, optionHeight);
    renderer.drawText(UI_10_FONT_ID, 20, optionY, tr(STR_APPLY_REMOTE), selectedOption != 0);

    if (selectedOption == 1) renderer.fillRect(0, optionY + optionHeight - 2, pageWidth - 1, optionHeight);
    renderer.drawText(UI_10_FONT_ID, 20, optionY + optionHeight, tr(STR_UPLOAD_LOCAL), selectedOption != 1);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) - 20, tr(STR_NO_REMOTE_MSG), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) + 10, tr(STR_UPLOAD_PROMPT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_UPLOAD), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == UPLOAD_COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                              tr(STR_UPLOAD_SUCCESS), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNC_FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) - 20, tr(STR_SYNC_FAILED_MSG), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight / 2) + 10, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void StorytellerSyncActivity::loop() {
  if (state == NO_TOKEN || state == NO_SERVER_URL || state == BOOK_NOT_FOUND || state == SYNC_FAILED ||
      state == UPLOAD_COMPLETE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }

  if (state == SHOWING_RESULT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedOption = (selectedOption + 1) % 2;
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedOption == 0) {
        // Apply remote position: return resolved CrossPoint coords to reader.
        setResult(SyncResult{remoteSpineIndex, remotePage});
        finish();
      } else {
        performUpload();
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      performUpload();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }
}
