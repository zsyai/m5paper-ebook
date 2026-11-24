#include "ui/widgets/keypad.h"

#include <algorithm>

#include "ui/widgets/controls.h"

namespace reader {
namespace ui {

namespace {

constexpr int kDeleteKey = 9;
constexpr int kZeroKey = 10;
constexpr int kOkKey = 11;
constexpr int kDotKey = 9;
constexpr int kDecimalDeleteKey = 11;
const char* const kIntKeyLabels[12] = {"1", "2", "3", "4",    "5", "6",
                                       "7", "8", "9", "删除", "0", "确定"};
const char* const kDecimalKeyLabels[12] = {"1", "2", "3", "4", "5", "6",
                                           "7", "8", "9", ".", "0", "删除"};

}  // namespace

void Keypad::Open(const std::string& title, int max_digits,
                  const std::vector<std::string>& modes, int mode) {
  open_ = true;
  allow_decimal_ = false;
  title_ = title;
  text_.clear();
  max_digits_ = max_digits;
  modes_ = modes;
  mode_ = modes.empty() ? 0 : std::min(std::max(mode, 0),
                                       static_cast<int>(modes.size()) - 1);
}

void Keypad::OpenDecimal(const std::string& title, int max_digits) {
  Open(title, max_digits);
  allow_decimal_ = true;
}

void Keypad::set_max_digits(int max_digits) {
  max_digits_ = max_digits;
  if (static_cast<int>(text_.size()) > max_digits) text_.resize(max_digits);
}

void Keypad::Layout(int canvas_w, int canvas_h, int fs) {
  const int pad = 20;
  const int gap = fs / 2;
  const int title_h = fs * 1.8;
  const int input_h = fs * 2;
  const int mode_h = modes_.empty() ? 0 : fs * 2 + gap;
  const int cell_h = fs * 2.8;
  const int btn_h = fs * 1.9;
  const int bottom_h = allow_decimal_ ? (gap + btn_h + pad)
                                      : static_cast<int>(fs * 2.6);
  const int w = std::min(static_cast<int>(fs * 13.3), canvas_w - 40);
  const int h = pad + title_h + input_h + gap + mode_h + 4 * cell_h + bottom_h;
  dialog_ = Rect{(canvas_w - w) / 2, (canvas_h - h) / 2, w, h};

  const int inner_x = dialog_.x + pad;
  const int inner_w = w - 2 * pad;
  int y = dialog_.y + pad;
  title_row_ = Rect{inner_x, y, inner_w, title_h};
  y += title_h;
  input_ = Rect{inner_x, y, inner_w, input_h};
  y += input_h + gap;

  mode_row_ = Rect{};
  if (!modes_.empty()) {
    mode_row_ = Rect{inner_x, y, inner_w, fs * 2};
    y += mode_h;
  }

  for (int i = 0; i < 12; ++i) {
    const int col = i % 3;
    const int row = i / 3;
    const int x0 = inner_x + inner_w * col / 3;
    const int x1 = inner_x + inner_w * (col + 1) / 3;
    keys_[i] = Rect{x0, y + row * cell_h, x1 - x0, cell_h};
  }
  y += 4 * cell_h;
  if (allow_decimal_) {
    y += gap;
    const int btn_w = (inner_w - gap) / 2;
    cancel_ = Rect{inner_x, y, btn_w, btn_h};
    ok_ = Rect{inner_x + inner_w - btn_w, y, btn_w, btn_h};
  } else {
    cancel_ = Rect{inner_x, y, inner_w, dialog_.bottom() - y};
    ok_ = Rect{};
  }
}

void Keypad::Draw(UiPainter& p) {
  if (!open_) return;
  LGFX_Sprite& canvas = p.canvas();
  const int fs = p.font_size();
  Layout(canvas.width(), canvas.height(), fs);

  canvas.fillRect(dialog_.x, dialog_.y, dialog_.w, dialog_.h, TFT_WHITE);
  canvas.drawRect(dialog_.x, dialog_.y, dialog_.w, dialog_.h, TFT_BLACK);
  canvas.drawRect(dialog_.x + 1, dialog_.y + 1, dialog_.w - 2, dialog_.h - 2,
                  TFT_BLACK);

  p.Prepare(title_);
  p.Text(title_, title_row_.x, p.CenterY(title_row_.y, title_row_.h));

  canvas.drawRect(input_.x, input_.y, input_.w, input_.h, TFT_BLACK);
  canvas.drawRect(input_.x + 1, input_.y + 1, input_.w - 2, input_.h - 2,
                  TFT_BLACK);
  p.Prepare(text_);
  const int text_x = input_.x + fs / 2;
  p.Text(text_, text_x, p.CenterY(input_.y, input_.h));
  const int cursor_x = text_x + p.Width(text_) + 2;
  canvas.drawFastVLine(cursor_x, input_.y + fs / 3, input_.h - fs * 2 / 3,
                       TFT_BLACK);

  if (!modes_.empty()) mode_control_.Draw(p, mode_row_, modes_, mode_);

  const char* const* labels =
      allow_decimal_ ? kDecimalKeyLabels : kIntKeyLabels;
  const Rect grid{keys_[0].x, keys_[0].y, keys_[2].right() - keys_[0].x,
                  keys_[kOkKey].bottom() - keys_[0].y};
  for (int i = 0; i < 12; ++i) {
    const Rect& key = keys_[i];
    const bool filled = !allow_decimal_ && (i == kOkKey);
    if (filled) canvas.fillRect(key.x, key.y, key.w, key.h, TFT_BLACK);
    p.TextIn(labels[i], key, filled);
  }
  for (int col = 1; col < 3; ++col) {
    canvas.drawFastVLine(keys_[col].x, grid.y, grid.h, TFT_BLACK);
  }
  for (int row = 1; row < 4; ++row) {
    canvas.drawFastHLine(grid.x, keys_[row * 3].y, grid.w, TFT_BLACK);
  }
  canvas.drawRect(grid.x, grid.y, grid.w, grid.h, TFT_BLACK);

  if (allow_decimal_) {
    DrawButton(p, cancel_, "取消");
    DrawButton(p, ok_, "确定", /*filled=*/true);
  } else {
    p.TextIn("取消", cancel_);
  }
}

Keypad::Key Keypad::Press(int x, int y) {
  if (!open_) return Key::kNone;
  if (!dialog_.Contains(x, y) || cancel_.Contains(x, y)) return Key::kCancel;
  if (allow_decimal_ && ok_.Contains(x, y)) return Key::kOk;
  if (!modes_.empty() && mode_row_.Contains(x, y)) {
    const int hit = mode_control_.HitTest(x, y);
    if (hit < 0 || hit == mode_) return Key::kNone;
    mode_ = hit;
    return Key::kMode;
  }
  for (int i = 0; i < 12; ++i) {
    if (!keys_[i].Contains(x, y)) continue;
    if (allow_decimal_) {
      if (i == kDecimalDeleteKey) {
        if (text_.empty()) return Key::kNone;
        text_.pop_back();
        return Key::kEdit;
      }
      if (i == kDotKey) {
        if (text_.find('.') != std::string::npos) return Key::kNone;
        if (text_.empty()) {
          if (max_digits_ < 2) return Key::kNone;
          text_ = "0.";
          return Key::kEdit;
        }
        if (static_cast<int>(text_.size()) >= max_digits_) return Key::kNone;
        text_ += '.';
        return Key::kEdit;
      }
      if (static_cast<int>(text_.size()) >= max_digits_) return Key::kNone;
      const size_t dot = text_.find('.');
      if (dot != std::string::npos && text_.size() - dot > 1) return Key::kNone;
      text_ += (i == kZeroKey) ? '0' : static_cast<char>('1' + i);
      return Key::kEdit;
    }
    if (i == kOkKey) return Key::kOk;
    if (i == kDeleteKey) {
      if (text_.empty()) return Key::kNone;
      text_.pop_back();
      return Key::kEdit;
    }
    if (static_cast<int>(text_.size()) >= max_digits_) return Key::kNone;
    text_ += (i == kZeroKey) ? '0' : static_cast<char>('1' + i);
    return Key::kEdit;
  }
  return Key::kNone;
}

}  // namespace ui
}  // namespace reader
