#pragma once

// Modal message box drawn over a page: a title, a few lines of text and one
// or two buttons ("取消" on the left, the action on the right in black).
// The page underneath is lightened so the box stands out.

#include <string>
#include <vector>

#include "ui/painter.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

class Dialog {
 public:
  enum class Result { kNone, kCancel, kConfirm };

  // An empty `cancel_label` shows only the confirm button.
  void Open(const std::string& title, const std::vector<std::string>& lines,
            const std::string& confirm_label,
            const std::string& cancel_label = "取消");
  void Close() { open_ = false; }
  bool is_open() const { return open_; }

  // Draws over the current canvas (call after the page is drawn).
  void Draw(UiPainter& p);
  // Taps outside the buttons are ignored.
  Result Press(int x, int y) const;

 private:
  bool open_ = false;
  std::string title_;
  std::vector<std::string> lines_;
  std::string confirm_label_;
  std::string cancel_label_;
  // Recorded by Draw().
  Rect confirm_;
  Rect cancel_;
};

}  // namespace ui
}  // namespace reader
