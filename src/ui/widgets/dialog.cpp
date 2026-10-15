#include "ui/widgets/dialog.h"

#include <cstdint>

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

}  // namespace ui
}  // namespace reader
