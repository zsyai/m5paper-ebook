#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_LAYOUT_SETTINGS_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_LAYOUT_SETTINGS_PAGE_H_

// 排版设置 (first reading-settings page): 正文字体, 正文字号, 行间距, 页边距,
// 段首缩进 + 行下虚线, 阅读方向, and a live body-text preview.

#include <string>
#include <vector>

#include "ui/page.h"
#include "ui/rect.h"
#include "ui/widgets/controls.h"
#include "ui/widgets/keypad.h"
#include "ui/widgets/setting_rows.h"

namespace reader {
namespace ui {

class LayoutSettingsPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void exit(app::AppContext& ctx) override { keypad_.Close(); }
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  void show(app::AppContext& ctx) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

 private:
  enum class Field { kNone, kBodyFontSize, kLineSpacing, kMargin };

  void stepBodyFont(app::AppContext& ctx, int delta);
  void stepField(app::AppContext& ctx, Field field, int direction);
  void openKeypad(app::AppContext& ctx, Field field);
  void applyKeypad(app::AppContext& ctx);
  void commit(app::AppContext& ctx);

  std::vector<std::string> font_options_;
  int font_index_ = 0;
  Field editing_ = Field::kNone;
  Keypad keypad_;

  SelectorRow font_row_;
  ValueStepper size_stepper_;
  ValueStepper spacing_stepper_;
  ValueStepper margin_stepper_;
  Rect indent_hit_;
  Rect underline_hit_;
  Segmented orientation_;
};

}  // namespace ui
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_LAYOUT_SETTINGS_PAGE_H_
