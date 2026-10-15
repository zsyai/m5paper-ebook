#pragma once

// Screen rectangle used by widgets for drawing and touch hit tests.

namespace reader {
namespace ui {

struct Rect {
  Rect() {}
  Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}

  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  int right() const { return x + w; }
  int bottom() const { return y + h; }
  int center_x() const { return x + w / 2; }
  int center_y() const { return y + h / 2; }
  bool empty() const { return w <= 0 || h <= 0; }
  bool Contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
  // Shrinks by `d` on every side.
  Rect Inset(int d) const { return Rect{x + d, y + d, w - 2 * d, h - 2 * d}; }
};

}  // namespace ui
}  // namespace reader
