#include "EpubReaderPercentSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Fine/coarse slider step sizes for percent adjustments.
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;
}  // namespace

void EpubReaderPercentSelectionActivity::onEnter() {
  Activity::onEnter();
  // Set up rendering task and mark first frame dirty.
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderPercentSelectionActivity::adjustPercent(const int delta) {
  // Apply delta and clamp within 0-100.
  percent += delta;
  if (percent < 0) {
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::loop() {
  // Back cancels, confirm selects, arrows adjust the percent.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(PercentResult{percent});
    finish();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustPercent(-kSmallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustPercent(kSmallStep); });

  // Side input is logically Up/Down regardless of device. On X4 the side
  // rocker is vertical, so "Up = increase" reads like a volume rocker. On
  // X3 the same logical buttons are two horizontal side buttons where the
  // left one (BTN_UP) physically suggests "back/decrease"; flipping the
  // sign on X3 gives the X3 user the expected left=decrease/right=increase
  // behavior without changing X4's muscle memory.
  const int largeSign = mappedInput.isX3Device() ? -1 : 1;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up},
                                       [this, largeSign] { adjustPercent(largeSign * kLargeStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this, largeSign] { adjustPercent(-largeSign * kLargeStep); });
}

void EpubReaderPercentSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Title and numeric percent value.
  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_GO_TO_PERCENT), true, EpdFontFamily::BOLD);

  const std::string percentText = std::to_string(percent) + "%";
  renderer.drawCenteredText(UI_12_FONT_ID, 90, percentText.c_str(), true, EpdFontFamily::BOLD);

  // Draw slider track.
  const int screenWidth = renderer.getScreenWidth();
  constexpr int barWidth = 360;
  constexpr int barHeight = 16;
  const int barX = (screenWidth - barWidth) / 2;
  const int barY = 140;

  renderer.drawRect(barX, barY, barWidth, barHeight);

  // Fill slider based on percent.
  const int fillWidth = (barWidth - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4);
  }

  // Draw a simple knob centered at the current percent.
  const int knobX = barX + 2 + fillWidth - 2;
  renderer.fillRect(knobX, barY - 4, 4, barHeight + 8, true);

  // Hint text for step sizes.
  renderer.drawCenteredText(SMALL_FONT_ID, barY + 30, tr(STR_PERCENT_STEP_HINT), true);

  // Button hints follow the current front button layout.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}
