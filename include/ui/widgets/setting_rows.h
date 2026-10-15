#pragma once

// Settings rows with a label on the left and a control on the right:
//   SelectorRow   "label        ◀ value ▶"   (cycles through text choices)
//   ValueStepper  "label      ◀ [ 36 ] ▶"   (numbers; tap the box to type)

#include <string>
#include <vector>

#include "ui/painter.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

// Horizontal padding of settings rows and their separators.
constexpr int kRowPaddingX = 20;

// Draws `label` at the left of `row` and a thin separator under it.
void DrawRowLabel(UiPainter& p, const Rect& row, const std::string& label);

class SelectorRow {
 public:
  // `value` is shortened with "..." to fit between the label and arrows.
  // Without arrows only the value is shown.
  void Draw(UiPainter& p, const Rect& row, const std::string& label,
            const std::string& value, bool has_arrows);
  // -1 for the left arrow, +1 for the right arrow, 0 otherwise.
  int HitTest(int x, int y) const;

 private:
  Rect left_;
  Rect right_;
  bool has_arrows_ = false;
};

class ValueStepper {
 public:
  enum class Hit { kNone, kDecrement, kValue, kIncrement };

  // Draws "◀ [value] ▶" right-aligned in `row` (the label is drawn by the
  // caller). Disabled steppers show the value without box or arrows.
  void Draw(UiPainter& p, const Rect& row, const std::string& value,
            bool enabled = true);
  Hit HitTest(int x, int y) const;

 private:
  Rect dec_;
  Rect value_;
  Rect inc_;
  bool enabled_ = false;
};

// `value` moved one `step` in `direction` (-1 or +1), kept in [lo, hi].
int StepLinear(int value, int step, int lo, int hi, int direction);

// Next preset above `value` (direction +1) or below it (-1); `value` itself
// need not be a preset. Stays put at either end. `presets` is ascending.
int StepPreset(int value, const std::vector<int>& presets, int direction);

}  // namespace ui
}  // namespace reader
