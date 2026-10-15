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

}  // namespace hal
}  // namespace reader
