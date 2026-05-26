#pragma once

class GfxRenderer;

// Tiny ring progress indicator used next to selected-book labels in the
// Recent Books grid and Book Stats pages. Renders an annulus centered at
// (cx, cy): outer radius = `radius`, inner hole left empty. The ring band
// fills clockwise from 12 o'clock proportional to `percent` (0..100). No
// outline is drawn around the band, so 0% (or unknown) progress is fully
// transparent; the band itself is the entire visual.
namespace ProgressRingUtil {
void drawProgressRing(const GfxRenderer& renderer, int cx, int cy, int radius, float percent);
}  // namespace ProgressRingUtil
