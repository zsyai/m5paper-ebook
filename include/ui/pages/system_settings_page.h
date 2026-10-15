#pragma once

// 系统设置, opened from the bookshelf: a row that opens 阅读设置, the UI
// font, the lock screen wallpaper, cleaning old reading records and unused
// font caches, and the user guide.

#include <string>
#include <vector>

#include "fonts/font_cache_store.h"
#include "ui/page.h"
#include "ui/rect.h"
#include "ui/widgets/dialog.h"
#include "ui/widgets/setting_rows.h"

namespace reader {
namespace ui {

class SystemSettingsPage : public Page {
 public:
  void enter() override;
  void exit() override { dialog_.Close(); }
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;

 private:
  void stepUiFont(int delta);
  // Opens the confirmation for 字体缓存 清理.
  void askCleanFontCaches();
  void cleanFontCaches();

  // UI font choices: "builtin" followed by the .ttf fonts in /fonts. .bin
  // fonts have a fixed size and would break the 30px UI layout.
  std::vector<std::string> ui_font_options_;
  int ui_font_index_ = 0;
  // Font cache sizes, scanned on enter() and after cleaning.
  fonts::FontCacheUsage cache_usage_;
  Dialog dialog_;

  // Hit areas, recorded by draw().
  Rect reading_row_;
  SelectorRow ui_font_row_;
  Rect lock_toggle_;
  Rect wallpaper_button_;
  Rect clean_button_;
  Rect cache_button_;
  Rect guide_button_;
};

}  // namespace ui
}  // namespace reader
