#include "ProgressRingUtil.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>

namespace ProgressRingUtil {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

void drawProgressRing(const GfxRenderer& renderer, const int cx, const int cy, const int radius, const float percent) {
  if (radius <= 0) return;

  const float clamped = std::clamp(percent, 0.0f, 100.0f);
  const float fraction = clamped / 100.0f;
  const float endAngle = fraction * 2.0f * kPi;
  const bool fullyFilled = fraction >= 1.0f;
  const bool anyFill = fraction > 0.0f;

  // 7-px band — chunky enough to read clearly at the small radius used next
  // to UI_12 titles, while still leaving a visible inner hole.
  const int bandThickness = std::min(radius, 7);
  const int outerR = radius;
  const int innerR = std::max(0, radius - bandThickness);
  const int outerSq = outerR * outerR;
  const int innerSq = innerR * innerR;

  for (int dy = -outerR; dy <= outerR; ++dy) {
    for (int dx = -outerR; dx <= outerR; ++dx) {
      const int d2 = dx * dx + dy * dy;
      if (d2 > outerSq || d2 <= innerSq) continue;  // ring band only

      bool fillPixel = fullyFilled;
      if (!fillPixel && anyFill) {
        // atan2(dx, -dy): 0 at 12 o'clock, growing clockwise to 2π.
        float a = std::atan2(static_cast<float>(dx), static_cast<float>(-dy));
        if (a < 0.0f) a += 2.0f * kPi;
        if (a <= endAngle) fillPixel = true;
      }
      if (fillPixel) {
        renderer.drawPixel(cx + dx, cy + dy, true);  // black for read-progress
      } else {
        // Unfilled portion of the ring: standard LightGray dither (1-in-4
        // 2×2 grid). Custom-stride patterns looked too pixelated at this
        // radius — the built-in dither matches the rest of the firmware.
        renderer.fillRectDither(cx + dx, cy + dy, 1, 1, Color::LightGray);
      }
    }
  }
}

}  // namespace ProgressRingUtil
