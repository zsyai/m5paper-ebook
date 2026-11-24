#include "ui/widgets/dialog.h"

#include <cstdint>

#include "ui/text_renderer.h"
#include "ui/widgets/controls.h"

namespace reader {
namespace ui {
namespace {

constexpr int kScreenPadding = 40;
constexpr int kBorder = 2;

// Lightens every pixel of the 8-bit grayscale canvas: black becomes mid
// grey, white stays white.
void Lighten(LGFX_Sprite& canvas) {
  uint8_t* px = static_cast<uint8_t*>(canvas.getBuffer());
  if (!px) return;
  const size_t n = static_cast<size_t>(canvas.width()) * canvas.height();
  for (size_t i = 0; i < n; ++i) {
    px[i] = static_cast<uint8_t>(255 - (255 - px[i]) * 2 / 5);
  }
}

}  // namespace

void Dialog::Open(const std::string& title,
                  const std::vector<std::string>& lines,
                  const std::string& confirm_label,
                  const std::string& cancel_label) {
  title_ = title;
  lines_ = lines;
  confirm_label_ = confirm_label;
  cancel_label_ = cancel_label;
  confirm_ = Rect();
  cancel_ = Rect();
  open_ = true;
}

void Dialog::Draw(UiPainter& p) {
  if (!open_) return;
  LGFX_Sprite& canvas = p.canvas();
  Lighten(canvas);

  const int fs = p.font_size();
  const int pad = fs * 0.8;
  const int line_h = fs * 1.5;
  const int button_h = fs * 1.8;
  const int n = static_cast<int>(lines_.size());
  const int box_w = canvas.width() - 2 * kScreenPadding;
  const int box_h = pad + line_h + fs / 2 + n * line_h + pad + button_h + pad;
  const Rect box{kScreenPadding, (canvas.height() - box_h) / 2, box_w, box_h};
  canvas.fillRect(box.x, box.y, box.w, box.h, TFT_WHITE);
  for (int i = 0; i < kBorder; ++i) {
    canvas.drawRect(box.x + i, box.y + i, box.w - 2 * i, box.h - 2 * i,
                    TFT_BLACK);
  }

  const int text_x = box.x + pad;
  int top = box.y + pad;
  p.Prepare(title_);
  p.Text(title_, text_x, p.CenterY(top, line_h));
  top += line_h + fs / 2;
  for (const std::string& line : lines_) {
    p.Prepare(line);
    p.Text(line, text_x, p.CenterY(top, line_h));
    top += line_h;
  }

  top += pad;
  const int button_w = (box.w - 3 * pad) / 2;
  if (cancel_label_.empty()) {
    cancel_ = Rect();
    confirm_ = Rect{box.center_x() - button_w / 2, top, button_w, button_h};
  } else {
    cancel_ = Rect{box.x + pad, top, button_w, button_h};
    confirm_ = Rect{box.right() - pad - button_w, top, button_w, button_h};
    DrawButton(p, cancel_, cancel_label_);
  }
  DrawButton(p, confirm_, confirm_label_, /*filled=*/true);
}

Dialog::Result Dialog::Press(int x, int y) const {
  if (!open_) return Result::kNone;
  if (confirm_.Contains(x, y)) return Result::kConfirm;
  if (cancel_.Contains(x, y)) return Result::kCancel;
  return Result::kNone;
}

void ActionMenu::Open(const std::string& title,
                      const std::vector<std::string>& items,
                      int selected_index) {
  title_ = title;
  items_ = items;
  selected_index_ = selected_index;
  box_ = Rect();
  item_rects_.clear();
  cancel_ = Rect();
  open_ = true;
}

void ActionMenu::Draw(UiPainter& p) {
  if (!open_) return;
  LGFX_Sprite& canvas = p.canvas();
  Lighten(canvas);

  const int fs = p.font_size();
  const int pad = fs * 0.8;
  const int header_h = fs * 2.3;
  const int item_h = fs * 2.4;
  const int button_h = fs * 1.8;
  const int n = static_cast<int>(items_.size());
  const int box_w = canvas.width() - 2 * kScreenPadding;
  const int box_h = header_h + n * item_h + pad + button_h + pad;
  box_ = Rect{kScreenPadding, (canvas.height() - box_h) / 2, box_w, box_h};

  canvas.fillRect(box_.x, box_.y, box_.w, box_.h, TFT_WHITE);
  for (int i = 0; i < kBorder; ++i) {
    canvas.drawRect(box_.x + i, box_.y + i, box_.w - 2 * i, box_.h - 2 * i,
                    TFT_BLACK);
  }

  p.Prepare(title_ + "...取消");
  const std::string short_title =
      Ellipsize(p.cache(), title_, box_.w - 2 * pad);
  p.TextIn(short_title, Rect{box_.x + pad, box_.y, box_.w - 2 * pad, header_h});
  canvas.fillRect(box_.x, box_.y + header_h - 2, box_.w, 2, TFT_BLACK);

  constexpr int kRowInset = 28;
  constexpr int kDotRadius = 6;
  constexpr uint8_t kDividerGray = 0xB8;
  item_rects_.clear();
  int top = box_.y + header_h;
  const int text_x = box_.x + kRowInset;
  const int dot_cx = box_.right() - kRowInset - 8;
  for (int i = 0; i < n; ++i) {
    p.Prepare(items_[i]);
    const Rect row{box_.x, top, box_.w, item_h};
    item_rects_.push_back(row);
    p.Text(items_[i], text_x, p.CenterY(top, item_h));
    if (i == selected_index_) {
      canvas.fillCircle(dot_cx, row.center_y(), kDotRadius, TFT_BLACK);
    }
    top += item_h;
    if (i + 1 < n) {
      canvas.drawFastHLine(
          box_.x + kRowInset, top - 1, box_.w - 2 * kRowInset,
          lgfx::color888(kDividerGray, kDividerGray, kDividerGray));
    }
  }

  top += pad;
  const int button_w = (box_.w - 3 * pad) / 2;
  cancel_ = Rect{box_.center_x() - button_w / 2, top, button_w, button_h};
  DrawButton(p, cancel_, "取消");
}

int ActionMenu::Press(int x, int y) const {
  if (!open_) return kNone;
  if (!box_.Contains(x, y) || cancel_.Contains(x, y)) return kCancel;
  for (size_t i = 0; i < item_rects_.size(); ++i) {
    if (item_rects_[i].Contains(x, y)) return static_cast<int>(i);
  }
  return kNone;
}

}  // namespace ui
}  // namespace reader
