#pragma once

// Input mapping for rotated pages.
//
// The panel and touch screen stay in their portrait frame; only the canvas
// is rotated (see `FrameService::UpdateRotation`). Touch points, swipes and
// wheel steps
// are converted here to the active canvas rotation.

#include <cstdint>

#include "core/settings.h"

namespace reader {
namespace hal {

// Portrait rotation (0 or 2) for pages that do not follow a landscape
// reading direction and for the sleep screen: normal portrait for ↓/←/→,
// upside-down portrait when the reading direction is ↑ (kDown).
inline uint8_t SystemRotation(core::Orientation orientation) {
  return orientation == core::Orientation::kDown ? 2 : 0;
}

// Converts a touch point from panel coordinates (portrait, `panel_w` x
// `panel_h`, as M5.Touch reports them) to the coordinates of a canvas drawn
// with LovyanGFX rotation `rotation` (0-3).
void PanelToCanvas(int rotation, int panel_w, int panel_h, int* x, int* y);

// Same as PanelToCanvas() for a movement (dx, dy) instead of a point.
void PanelDeltaToCanvas(int rotation, int* dx, int* dy);

// Converts a wheel step (+1 = towards the bottom of the portrait device) to
// a step (+1 = next / down / right) on a canvas with `rotation` (0-3).
int WheelToPageStep(int delta, int rotation);

// A finger movement shorter than this (in pixels) counts as a tap.
constexpr int kMinSwipeDistance = 60;

enum class Swipe { kNone, kLeft, kRight, kUp, kDown };

// Direction of a movement already in canvas coordinates: kNone if it is
// shorter than kMinSwipeDistance or too diagonal to tell.
Swipe ClassifySwipe(int dx, int dy);

}  // namespace hal
}  // namespace reader
