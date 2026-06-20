#include "StorytellerSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "StorytellerAuthActivity.h"
#include "StorytellerTokenStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void StorytellerSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void StorytellerSettingsActivity::handleSelection() {
  switch (selectedIndex) {
    case MENU_SET_SERVER_URL: {
      const std::string& current = ST_TOKEN_STORE.getServerUrl();
      const std::string prefill = current.empty() ? "https://" : current;
      startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ST_SERVER_URL),
                                                                     prefill.c_str(), 128, InputType::Url),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled) return;
                               const auto& entered = std::get<KeyboardResult>(result.data).text;
                               // Treat bare scheme as empty input
                               if (entered == "https://" || entered == "http://") {
                                 ST_TOKEN_STORE.setServerUrl("");
                               } else {
                                 ST_TOKEN_STORE.setServerUrl(entered);
                               }
                               ST_TOKEN_STORE.saveToFile();
                               requestUpdate();
                             });
      break;
    }

    case MENU_LINK_ACCOUNT:
      if (!ST_TOKEN_STORE.hasServerUrl()) {
        // Prompt user to set the URL first (just re-render with a nudge;
        // the server URL entry is item 0 which is already visible).
        return;
      }
      startActivityForResult(std::make_unique<StorytellerAuthActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;

    case MENU_UNLINK_ACCOUNT:
      ST_TOKEN_STORE.clearToken();
      requestUpdate();
      break;

    default:
      break;
  }
}

void StorytellerSettingsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (selectedIndex > 0) {
      selectedIndex--;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (selectedIndex < MENU_COUNT - 1) {
      selectedIndex++;
      requestUpdate();
    }
    return;
  }
}

void StorytellerSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_ST_SETTINGS));

  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int itemHeight = lineH + 8;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // --- Set Server URL ---
  if (selectedIndex == MENU_SET_SERVER_URL) renderer.fillRect(0, y - 2, pageWidth - 1, itemHeight);
  renderer.drawText(UI_10_FONT_ID, 20, y, tr(STR_ST_SERVER_URL), selectedIndex != MENU_SET_SERVER_URL);
  if (ST_TOKEN_STORE.hasServerUrl()) {
    char urlBuf[64];
    snprintf(urlBuf, sizeof(urlBuf), "  %s", ST_TOKEN_STORE.getServerUrl().c_str());
    renderer.drawText(UI_10_FONT_ID, 20, y + lineH, urlBuf);
    y += itemHeight + lineH;
  } else {
    y += itemHeight;
  }

  // --- Link Account ---
  if (selectedIndex == MENU_LINK_ACCOUNT) renderer.fillRect(0, y - 2, pageWidth - 1, itemHeight);
  const bool linked = ST_TOKEN_STORE.hasToken();
  renderer.drawText(UI_10_FONT_ID, 20, y, tr(STR_ST_LINK_ACCOUNT), selectedIndex != MENU_LINK_ACCOUNT);
  if (linked) {
    renderer.drawText(UI_10_FONT_ID, 20, y + lineH, tr(STR_ST_LINKED));
    y += itemHeight + lineH;
  } else {
    y += itemHeight;
  }

  // --- Unlink Account ---
  if (selectedIndex == MENU_UNLINK_ACCOUNT) renderer.fillRect(0, y - 2, pageWidth - 1, itemHeight);
  renderer.drawText(UI_10_FONT_ID, 20, y, tr(STR_ST_UNLINK_ACCOUNT), selectedIndex != MENU_UNLINK_ACCOUNT || !linked);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
