#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_USER_GUIDE_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_USER_GUIDE_PAGE_H_

// Built-in user guide page, laid out with book::MeasurePage and drawn with the
// UI font.

#include <string>
#include <vector>

#include "i18n/i18n.h"
#include "ui/page.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

class UserGuidePage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

 private:
  std::vector<std::string> pages_;
  std::string layout_font_path_;
  i18n::Language layout_language_ = i18n::Language::kZhHans;
  int current_page_idx_ = 0;
  Rect back_;

  void paginate(app::AppContext& ctx, LGFX_Sprite& canvas);
};

}  // namespace ui
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_USER_GUIDE_PAGE_H_
