#pragma once

// Modal message box drawn over a page: a title, a few lines of text and one
// or two buttons ("取消" on the left, the action on the right in black).
// The page underneath is lightened so the box stands out.

#include <string>
#include <vector>

#include "i18n/i18n.h"
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
            const std::string& cancel_label = TR("取消"));
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

// Modal action menu drawn over a page (e.g. long-pressing a book on the
// bookshelf): centered title header, full-width item rows with dividers, and a
// centered "取消" button.
class ActionMenu {
 public:
  static constexpr int kNone = -1;
  static constexpr int kCancel = -2;

  void Open(const std::string& title, const std::vector<std::string>& items,
            int selected_index = -1);
  void Close() { open_ = false; }
  bool is_open() const { return open_; }

  void Draw(UiPainter& p);
  // Returns 0..N-1 for a tapped item, kCancel for "取消" or a tap outside the
  // box, or kNone otherwise.
  int Press(int x, int y) const;

 private:
  bool open_ = false;
  int selected_index_ = -1;
  std::string title_;
  std::vector<std::string> items_;
  Rect box_;
  std::vector<Rect> item_rects_;
  Rect cancel_;
};

}  // namespace ui
}  // namespace reader
