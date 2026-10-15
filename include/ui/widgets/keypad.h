#pragma once

// Numeric keypad dialog, centered on the canvas:
//
//   title
//   [ input| ]
//   [ mode 0 | mode 1 ]      (optional, e.g. 按页码 / 按百分比)
//   1 2 3 / 4 5 6 / 7 8 9 / 删除 0 确定
//   取消
//
// Drawn on top of whatever the canvas already shows. The owner keeps the
// keypad open or closed, draws it, and passes taps to Press().

#include <string>
#include <vector>

#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/controls.h"

namespace reader {
namespace ui {

class Keypad {
 public:
  enum class Key { kNone, kEdit, kMode, kOk, kCancel };

  // Opens with empty input. `modes` (at most a few short labels) adds a row
  // of mode buttons; `mode` is the selected one.
  void Open(const std::string& title, int max_digits,
            const std::vector<std::string>& modes = {}, int mode = 0);
  void Close() { open_ = false; }
  bool is_open() const { return open_; }

  void set_title(const std::string& title) { title_ = title; }
  // Also drops digits beyond the new limit.
  void set_max_digits(int max_digits);
  const std::string& text() const { return text_; }
  int mode() const { return mode_; }

  void Draw(UiPainter& p);

  // Handles a tap at (x, y). Digits and 删除 edit the text (kEdit), a mode
  // button changes mode() (kMode). kOk / kCancel are left to the caller;
  // a tap outside the dialog counts as kCancel. Only valid after Draw().
  Key Press(int x, int y);

 private:
  void Layout(int canvas_w, int canvas_h, int fs);

  bool open_ = false;
  std::string title_;
  std::string text_;
  int max_digits_ = 0;
  std::vector<std::string> modes_;
  int mode_ = 0;

  Rect dialog_;
  Rect title_row_;
  Rect input_;
  Rect mode_row_;
  Segmented mode_control_;
  Rect keys_[12];  // 1-9, 删除, 0, 确定
  Rect cancel_;
};

}  // namespace ui
}  // namespace reader
