#pragma once

// 阅读设置. Opened from 系统设置 or from the reader menu, with the same
// layout either way; 返回 goes back to where it was opened from.
//
// Rows: 正文字体, 正文字号, 行间距, 页边距, 段首缩进 + 行下虚线, 阅读方向,
// 全刷间隔, then a preview of the body text. Number rows open a keypad when
// their value is tapped. The page is always drawn in portrait.

#include <string>
#include <vector>

#include "page.h"
#include "ui/rect.h"
#include "ui/widgets/controls.h"
#include "ui/widgets/keypad.h"
#include "ui/widgets/setting_rows.h"

namespace reader {
namespace ui {

class ReadingSettingsPage : public Page {
 public:
  void enter() override;
  void exit() override { keypad_.Close(); }
  void draw(LGFX_Sprite& canvas) override;
  // Keypad updates use the fast refresh mode.
  void show() override;
  State handleEvent(const SystemMessage_t& msg) override;

 private:
  // Setting edited with the keypad.
  enum class Field { kNone, kBodyFontSize, kMargin, kFullRefreshPages };

  void stepBodyFont(int delta);
  // Applies an arrow tap (-1 / +1) on a number field.
  void stepField(Field field, int direction);
  void openKeypad(Field field);
  // Applies the keypad value to the field being edited.
  void applyKeypad();
  // Saves the settings and redraws.
  void commit();

  State return_state_ = State::SETTING;
  // Body font choices: "builtin" followed by every font in /fonts.
  std::vector<std::string> font_options_;
  int font_index_ = 0;
  Field editing_ = Field::kNone;
  Keypad keypad_;

  // Hit areas, recorded by draw().
  SelectorRow font_row_;
  ValueStepper size_stepper_;
  Segmented spacing_;
  ValueStepper margin_stepper_;
  Rect indent_hit_;
  Rect underline_hit_;
  Segmented orientation_;
  ValueStepper refresh_stepper_;
  Rect back_;
};

}  // namespace ui
}  // namespace reader
