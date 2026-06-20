#pragma once

#include "StorytellerSyncClient.h"
#include "activities/Activity.h"

class StorytellerAuthActivity : public Activity {
 public:
  StorytellerAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("StorytellerAuth", renderer, mappedInput) {}

  bool preventAutoSleep() override { return true; }

  void onEnter() override;
  void onExit() override {}
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State {
    CONNECTING,
    REQUESTING_CODE,
    WAITING_FOR_USER,
    POLLING,
    SUCCESS,
    EXPIRED,
    DENIED,
    FAILED,
  };

  void onWifiSelectionComplete(bool success);
  void requestCode();
  void doPoll();

  State state = CONNECTING;

  char deviceCode[256] = {};
  char userCode[32] = {};
  char verificationUri[256] = {};
  int pollIntervalSec = 5;
  unsigned long pollExpireAt = 0;
  unsigned long nextPollAt = 0;
  unsigned long lastTimerRefresh = 0;
  int networkRetries = 0;
};
