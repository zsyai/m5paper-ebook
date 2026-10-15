#include "ui/widgets/controls.h"

namespace reader {
namespace ui {

namespace {
constexpr int kCornerRadius = 4;
}  // namespace

void DrawArrow(LGFX_Sprite& canvas, int cx, int cy, int size, bool right) {
  const int half_w = size / 2;
  const int tip_x = right ? cx + half_w : cx - half_w;
  const int base_x = right ? cx - half_w : cx + half_w;
  canvas.fillTriangle(tip_x, cy, base_x, cy - size, base_x, cy + size,
                      TFT_BLACK);
}

void DrawButton(UiPainter& p, const Rect& r, const std::string& label,
                bool filled) {
  LGFX_Sprite& canvas = p.canvas();
  if (filled) {
    canvas.fillRoundRect(r.x, r.y, r.w, r.h, kCornerRadius, TFT_BLACK);
  } else {
    canvas.fillRoundRect(r.x, r.y, r.w, r.h, kCornerRadius, TFT_WHITE);
    canvas.drawRoundRect(r.x, r.y, r.w, r.h, kCornerRadius, TFT_BLACK);
  }
  p.TextIn(label, r, filled);
}

void DrawToggle(UiPainter& p, const Rect& r, bool on) {
  DrawButton(p, r, on ? "开" : "关", on);
}

void Segmented::Draw(UiPainter& p, const Rect& area,
                     const std::vector<std::string>& labels, int selected) {
  LGFX_Sprite& canvas = p.canvas();
  cells_.clear();
  const int n = static_cast<int>(labels.size());
  if (n == 0) return;
  canvas.fillRect(area.x, area.y, area.w, area.h, TFT_WHITE);
  for (int i = 0; i < n; ++i) {
    // Spread the rounding error so the last cell ends at the right edge.
    const int x0 = area.x + area.w * i / n;
    const int x1 = area.x + area.w * (i + 1) / n;
    const Rect cell{x0, area.y, x1 - x0, area.h};
    cells_.push_back(cell);
    const bool on = (i == selected);
    if (on) canvas.fillRect(cell.x, cell.y, cell.w, cell.h, TFT_BLACK);
    if (i > 0) canvas.drawFastVLine(cell.x, cell.y, cell.h, TFT_BLACK);
    p.TextIn(labels[i], cell, on);
  }
  canvas.drawRect(area.x, area.y, area.w, area.h, TFT_BLACK);
}

int Segmented::HitTest(int x, int y) const {
  for (size_t i = 0; i < cells_.size(); ++i) {
    if (cells_[i].Contains(x, y)) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace ui
}  // namespace reader
