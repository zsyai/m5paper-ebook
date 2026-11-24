#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_SYSTEM_SETTINGS_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_SYSTEM_SETTINGS_PAGE_H_

// 系统设置, opened from the bookshelf: 阅读设置 entry, UI font, sleep mode,
// lock-screen wallpaper, cleaning old reading records and unused font caches,
// and the user guide.

#include <string>
#include <vector>

#include "book/reading_records.h"
#include "fonts/font_cache_store.h"
#include "ui/page.h"
#include "ui/rect.h"
#include "ui/widgets/dialog.h"
#include "ui/widgets/setting_rows.h"

namespace reader {
namespace ui {

class SystemSettingsPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void exit(app::AppContext& ctx) override { dialog_.Close(); }
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

 private:
  void stepUiFont(app::AppContext& ctx, int delta);
  void askCleanFontCaches(app::AppContext& ctx);
  void cleanFontCaches(app::AppContext& ctx);

  std::vector<std::string> ui_font_options_;
  int ui_font_index_ = 0;
  book::ExpiredRecordsUsage expired_records_;
  fonts::FontCacheUsage cache_usage_;
  Dialog dialog_;

  Rect reading_row_;
  SelectorRow ui_font_row_;
  SelectorRow sleep_row_;
  Rect lock_toggle_;
  Rect wallpaper_button_;
  Rect clean_button_;
  Rect cache_button_;
  Rect guide_button_;
};

}  // namespace ui
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_SYSTEM_SETTINGS_PAGE_H_
