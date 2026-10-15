#include "ui/widgets/setting_rows.h"

#include <algorithm>

#include "ui/text_renderer.h"
#include "ui/widgets/controls.h"

namespace reader {
namespace ui {

namespace {

// Arrow half-height and tap target width, relative to the UI font size.
int ArrowSize(int fs) { return fs * 0.4; }
int ArrowTargetW(int fs) { return fs * 1.6; }

}  // namespace

void DrawRowLabel(UiPainter& p, const Rect& row, const std::string& label) {
  p.Prepare(label);
  p.Text(label, row.x + kRowPaddingX, p.CenterY(row.y, row.h));
  p.canvas().drawFastHLine(row.x + kRowPaddingX, row.bottom(),
                           row.w - 2 * kRowPaddingX, TFT_BLACK);
}

void SelectorRow::Draw(UiPainter& p, const Rect& row, const std::string& label,
                       const std::string& value, bool has_arrows) {
  const int fs = p.font_size();
  DrawRowLabel(p, row, label);
  p.Prepare(value + "...");

  const int right = row.right() - kRowPaddingX;
  const int cy = row.center_y();
  const int right_arrow_cx = right - fs / 2;
  const int value_right = has_arrows ? right - fs * 1.4 : right;
  const int label_end = row.x + kRowPaddingX + p.Width(label);
  const int max_w = value_right - label_end - fs * 2;
  const std::string shown = Ellipsize(p.cache(), value, max_w);
  const int value_x = value_right - p.Width(shown);
  p.Text(shown, value_x, p.CenterY(row.y, row.h));

  has_arrows_ = has_arrows;
  if (!has_arrows) return;
  const int left_arrow_cx = value_x - fs * 0.9;
  DrawArrow(p.canvas(), left_arrow_cx, cy, ArrowSize(fs), /*right=*/false);
  DrawArrow(p.canvas(), right_arrow_cx, cy, ArrowSize(fs), /*right=*/true);
  const int target_w = ArrowTargetW(fs);
  left_ = Rect{left_arrow_cx - target_w / 2, row.y, target_w, row.h};
  right_ = Rect{right_arrow_cx - target_w / 2, row.y, target_w, row.h};
}

int SelectorRow::HitTest(int x, int y) const {
  if (!has_arrows_) return 0;
  if (left_.Contains(x, y)) return -1;
  if (right_.Contains(x, y)) return +1;
  return 0;
}

void ValueStepper::Draw(UiPainter& p, const Rect& row, const std::string& value,
                        bool enabled) {
  const int fs = p.font_size();
  const int right = row.right() - kRowPaddingX;
  const int cy = row.center_y();
  enabled_ = enabled;
  if (!enabled) {
    p.Prepare(value);
    p.Text(value, right - p.Width(value), p.CenterY(row.y, row.h));
    return;
  }
  const int box_h = fs * 1.8;
  const int box_w = fs * 3;
  const int box_right = right - fs * 1.4;
  value_ = Rect{box_right - box_w, cy - box_h / 2, box_w, box_h};
  DrawButton(p, value_, value);

  const int inc_cx = right - fs / 2;
  const int dec_cx = value_.x - fs * 0.8;
  DrawArrow(p.canvas(), dec_cx, cy, ArrowSize(fs), /*right=*/false);
  DrawArrow(p.canvas(), inc_cx, cy, ArrowSize(fs), /*right=*/true);
  const int target_w = ArrowTargetW(fs);
  dec_ = Rect{dec_cx - target_w / 2, row.y, target_w, row.h};
  inc_ = Rect{inc_cx - target_w / 2, row.y, target_w, row.h};
}

ValueStepper::Hit ValueStepper::HitTest(int x, int y) const {
  if (!enabled_) return Hit::kNone;
  if (value_.Contains(x, y)) return Hit::kValue;
  if (dec_.Contains(x, y)) return Hit::kDecrement;
  if (inc_.Contains(x, y)) return Hit::kIncrement;
  return Hit::kNone;
}

int StepLinear(int value, int step, int lo, int hi, int direction) {
  return std::min(hi, std::max(lo, value + direction * step));
}

int StepPreset(int value, const std::vector<int>& presets, int direction) {
  if (direction > 0) {
    for (int preset : presets) {
      if (preset > value) return preset;
    }
  } else {
    for (auto it = presets.rbegin(); it != presets.rend(); ++it) {
      if (*it < value) return *it;
    }
  }
  return value;
}

}  // namespace ui
}  // namespace reader
