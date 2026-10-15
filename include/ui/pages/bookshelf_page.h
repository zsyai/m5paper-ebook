#pragma once

// Bookshelf page: lists books in /books with reading progress, pagination, and
// bottom navigation to file transfer, settings, and sleep.

#include <vector>

#include "book/bookshelf.h"
#include "ui/page.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

class BookshelfPage : public Page {
 public:
  void init() {}

  void enter() override;
  void exit() override {}
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;

 private:
  Bookshelf bookshelf_;
  int page_index_ = 0;
  std::vector<Rect> item_rects_;
  Rect prev_page_rect_;
  Rect next_page_rect_;

  void nextBookshelfPage();
  void prevBookshelfPage();
  int itemsPerPage(const LGFX_Sprite& canvas) const;
};

}  // namespace ui
}  // namespace reader
