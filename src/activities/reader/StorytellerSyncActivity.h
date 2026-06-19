#pragma once

#include <memory>
#include <string>

#include "Epub.h"
#include "StorytellerSyncClient.h"
#include "activities/Activity.h"
#include "activities/ActivityResult.h"

class StorytellerSyncActivity : public Activity {
 public:
  StorytellerSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::shared_ptr<Epub> epub, std::string epubPath, int currentSpineIndex,
                          int currentPage, int totalPagesInSpine)
      : Activity(renderer, mappedInput),
        epub(std::move(epub)),
        epubPath(std::move(epubPath)),
        currentSpineIndex(currentSpineIndex),
        currentPage(currentPage),
        totalPagesInSpine(totalPagesInSpine) {}

  bool preventAutoSleep() const override {
    return state == RESOLVING_BOOK || state == SYNCING || state == UPLOADING;
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State {
    NO_TOKEN,
    NO_SERVER_URL,
    RESOLVING_BOOK,
    BOOK_NOT_FOUND,
    SYNCING,
    SHOWING_RESULT,
    NO_REMOTE_PROGRESS,
    UPLOADING,
    UPLOAD_COMPLETE,
    SYNC_FAILED,
  };

  void onWifiSelectionComplete(bool success);
  void resolveBookUuid();
  void performSync();
  void performUpload();

  std::shared_ptr<Epub> epub;
  std::string epubPath;
  int currentSpineIndex;
  int currentPage;
  int totalPagesInSpine;

  State state = SYNCING;
  std::string statusMessage;

  char bookUuid[64] = {};
  int selectedOption = 0;  // 0 = apply remote, 1 = upload local

  StorytellerPosition remotePosition;
  // Converted remote position in CrossPoint coordinates for display
  int remoteSpineIndex = 0;
  int remotePage = 0;
  float remoteProgressPct = 0.0f;
  float localProgressPct = 0.0f;
};
