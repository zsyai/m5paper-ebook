#pragma once

// Small controls shared by the settings pages and dialogs.

#include <string>
#include <vector>

#include "ui/painter.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

// Filled triangle pointing left or right, centered at (cx, cy); `size` is
// half its height.
void DrawArrow(LGFX_Sprite& canvas, int cx, int cy, int size, bool right);

// Rounded box with a centered label; `filled` draws white on black.
void DrawButton(UiPainter& p, const Rect& r, const std::string& label,
                bool filled = false);

// "开" on black when on, "关" in an outlined box when off.
void DrawToggle(UiPainter& p, const Rect& r, bool on);

// Row of equal cells with one selected (black) cell, e.g. line spacing.
class Segmented {
 public:
  void Draw(UiPainter& p, const Rect& area,
            const std::vector<std::string>& labels, int selected);
  // Index of the tapped cell, or -1.
  int HitTest(int x, int y) const;

 private:
  std::vector<Rect> cells_;
};

}  // namespace ui
}  // namespace reader
