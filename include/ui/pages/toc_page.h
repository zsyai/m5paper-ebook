#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_TOC_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_TOC_PAGE_H_

// Table-of-contents page (`State::READER_TOC`): portrait chapter list with
// hierarchical indentation, current-chapter dot indicator, right-aligned page
// numbers, wheel/swipe/button pagination, and a `返回 | 跳转` bottom bar.

#include <vector>

#include "ui/page.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

class TocPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

 private:
  int itemsPerPage(const app::AppContext& ctx,
                   const LGFX_Sprite& canvas) const;
  void nextTocPage(app::AppContext& ctx);
  void prevTocPage(app::AppContext& ctx);
  void clampPageIndex(const app::AppContext& ctx);

  int page_index_ = 0;
  std::vector<Rect> row_rects_;
  Rect prev_page_rect_;
  Rect next_page_rect_;
};

}  // namespace ui
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_TOC_PAGE_H_
