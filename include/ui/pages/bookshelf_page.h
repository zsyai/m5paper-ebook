#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_BOOKSHELF_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_BOOKSHELF_PAGE_H_

// Bookshelf page: lists books in /books with reading progress, pagination, and
// bottom navigation to file transfer, settings, and sleep.

#include <string>
#include <vector>

#include "book/bookshelf.h"
#include "ui/page.h"
#include "ui/rect.h"
#include "ui/widgets/dialog.h"

namespace reader {
namespace ui {

class BookshelfPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;
  const std::vector<BookInfo>& books() const {
    return bookshelf_.getBookList();
  }

 private:
  Bookshelf bookshelf_;
  int page_index_ = 0;
  std::vector<Rect> item_rects_;
  Rect sort_rect_;
  Rect prev_page_rect_;
  Rect next_page_rect_;
  ActionMenu sort_menu_;
  ActionMenu action_menu_;
  Dialog confirm_dialog_;
  std::string selected_book_;
  std::string selected_title_;

  void nextBookshelfPage(app::AppContext& ctx);
  void prevBookshelfPage(app::AppContext& ctx);
  void clampPageIndex(const app::AppContext& ctx);
  void openDeleteConfirm(app::AppContext& ctx);
  void clearSelectedBookRecord(app::AppContext& ctx);
  void deleteSelectedBook(app::AppContext& ctx);
  int itemsPerPage(const app::AppContext& ctx, const LGFX_Sprite& canvas) const;
};

}  // namespace ui
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_BOOKSHELF_PAGE_H_
