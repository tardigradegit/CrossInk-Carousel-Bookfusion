#pragma once

#include "activities/Activity.h"

class StorytellerSettingsActivity : public Activity {
 public:
  StorytellerSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("StorytellerSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum MenuItem {
    MENU_SET_SERVER_URL = 0,
    MENU_LINK_ACCOUNT,
    MENU_UNLINK_ACCOUNT,
    MENU_COUNT,
  };

  void handleSelection();

  int selectedIndex = 0;
};
