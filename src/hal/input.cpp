#include "hal/input.h"

namespace reader {
namespace hal {

void PanelToCanvas(int rotation, int panel_w, int panel_h, int* x, int* y) {
  const int px = *x;
  const int py = *y;
  // Inverse of the sprite mapping in ui/canvas_pixels.h.
  switch (rotation & 3) {
    case 1:  // Logical top at the panel's right edge.
      *x = py;
      *y = panel_w - 1 - px;
      break;
    case 2:  // Upside down.
      *x = panel_w - 1 - px;
      *y = panel_h - 1 - py;
      break;
    case 3:  // Logical top at the panel's left edge.
      *x = panel_h - 1 - py;
      *y = px;
      break;
    default:
      break;
  }
}

int WheelToPageStep(int delta, int rotation) {
  switch (rotation & 3) {
    case 0:  // Normal portrait.
    case 1:  // Text top towards the wheel: device bottom is reader's right.
      return delta;
    case 2:  // Upside-down portrait.
    case 3:  // Text top away from the wheel: device bottom is reader's left.
      return -delta;
  }
  return delta;
}

void PanelDeltaToCanvas(int rotation, int* dx, int* dy) {
  const int px = *dx;
  const int py = *dy;
  // PanelToCanvas() without the translation.
  switch (rotation & 3) {
    case 1:
      *dx = py;
      *dy = -px;
      break;
    case 2:
      *dx = -px;
      *dy = -py;
      break;
    case 3:
      *dx = -py;
      *dy = px;
      break;
    default:
      break;
  }
}

Swipe ClassifySwipe(int dx, int dy) {
  const int ax = dx < 0 ? -dx : dx;
  const int ay = dy < 0 ? -dy : dy;
  // The main axis must be clearly longer than the other one.
  if (ax >= kMinSwipeDistance && ax * 2 >= ay * 3) {
    return dx < 0 ? Swipe::kLeft : Swipe::kRight;
  }
  if (ay >= kMinSwipeDistance && ay * 2 >= ax * 3) {
    return dy < 0 ? Swipe::kUp : Swipe::kDown;
  }
  return Swipe::kNone;
}

}  // namespace hal
}  // namespace reader
