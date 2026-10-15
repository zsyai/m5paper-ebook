#include "ui/pages/bookshelf_page.h"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "core/perf_trace.h"
#include "ui/text_renderer.h"
#include "ui/widgets/bars.h"
#include "app/ui_manager.h"

extern UIManager ui;

namespace reader {
namespace ui {

namespace {

std::string FormatBookInfo(const BookInfo& book) {
  std::string status;
  if (book.progress >= 100) {
    status = book.isLastRead ? "上次读完" : "已读完";
  } else if (book.isLastRead) {
    status = "上次阅读" + std::to_string(book.progress) + "%";
  } else if (book.pos > 0) {
    status = "已读" + std::to_string(book.progress) + "%";
  } else {
    status = "未读";
  }

  char size_buf[32];
  if (book.size < 1024 * 1024) {
    snprintf(size_buf, sizeof(size_buf), "%uKB", book.size / 1024);
  } else {
    snprintf(size_buf, sizeof(size_buf), "%.1fMB",
             book.size / (1024.0 * 1024.0));
  }
  return status + " · " + size_buf;
}

}  // namespace

void BookshelfPage::enter() {
  bookshelf_.refresh();
  const int per_page = itemsPerPage(::ui.getCanvas());
  const int total = static_cast<int>(bookshelf_.getBookList().size());
  if (page_index_ * per_page >= total) {
    page_index_ = std::max(0, (total - 1) / per_page);
  }
}

void BookshelfPage::draw(LGFX_Sprite& canvas) {
  PERF_SCOPE("BookshelfPage::draw");
  UiPainter& p = ::ui.painter();
  const std::vector<BookInfo>& books = bookshelf_.getBookList();

  std::string preload =
      "我的书架已读完未读上次阅读上一页下一页传书设置休眠·%0123456789.MBKB ";
  if (!::ui.hasTFCard()) {
    preload += "未检测到TF卡";
  } else if (books.empty()) {
    preload += "未找到书籍。";
  }
  if (::ui.isUsingBuiltinFont()) {
    preload += "字体文件缺失，正在使用内置字体请通过“传书”上传字体文件";
  }
  for (const auto& book : books) {
    preload += book.filename.c_str();
  }
  p.Prepare(preload);

  canvas.fillScreen(TFT_WHITE);

  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const int header_h = ::ui.getHeaderHeight();
  const int footer_h = ::ui.getFooterHeight();
  const int pager_h = fs * 1.5f;
  const int pager_top = screen_h - footer_h - pager_h;

  ::ui.drawHeader("我的书架");

  const int item_h = fs * 3 + 40;
  const int per_page = itemsPerPage(canvas);
  const int start_idx = page_index_ * per_page;
  const int end_idx = std::min(static_cast<int>(books.size()),
                               start_idx + per_page);

  item_rects_.clear();
  int item_y = header_h - 4 + 10;
  for (int i = start_idx; i < end_idx; ++i) {
    item_rects_.push_back(Rect{0, item_y, screen_w, item_h});

    const std::string name = Ellipsize(
        p.cache(), std::string(books[i].filename.c_str()), screen_w - 60);
    p.Text(name, 20, item_y + 20);

    const std::string info = FormatBookInfo(books[i]);
    p.Text(info, 40, item_y + 20 + static_cast<int>(fs * 1.5f));

    item_y += item_h;
    canvas.drawFastHLine(0, item_y - 5, screen_w, TFT_BLACK);
  }

  if (::ui.hasTFCard() && books.empty()) {
    p.Text("未找到书籍。", 20, item_y + 20);
  }

  prev_page_rect_ = Rect{};
  next_page_rect_ = Rect{};
  if (page_index_ > 0) {
    prev_page_rect_ = Rect{0, pager_top, screen_w / 2, pager_h};
    p.Text("上一页", screen_w / 4 - p.Width("上一页") / 2, pager_top);
  }
  if (end_idx < static_cast<int>(books.size())) {
    next_page_rect_ = Rect{screen_w / 2, pager_top, screen_w / 2, pager_h};
    p.Text("下一页", screen_w * 3 / 4 - p.Width("下一页") / 2, pager_top);
  }

  ::ui.drawBottomBar({"传书", "设置", "休眠"});

  if (!::ui.hasTFCard()) {
    DrawMessageBox(p, {"未检测到TF卡"});
  } else if (::ui.isUsingBuiltinFont()) {
    DrawMessageBox(p, {"字体文件缺失，正在使用内置字体",
                       "请通过“传书”上传字体文件"});
  }
}

State BookshelfPage::handleEvent(const SystemMessage_t& msg) {
  const std::vector<BookInfo>& books = bookshelf_.getBookList();

  if (msg.type == MessageType::WHEEL) {
    if (msg.data.wheel.delta > 0) {
      nextBookshelfPage();
      return State::BOOKSHELF;
    }
    if (msg.data.wheel.delta < 0) {
      prevBookshelfPage();
      return State::BOOKSHELF;
    }
  }

  if (msg.type != MessageType::TOUCH) return State::BOOKSHELF;
  const int x = msg.data.touch.x;
  const int y = msg.data.touch.y;

  const int footer_col = BottomBarHit(::ui.getCanvas(), ::ui.getFooterHeight(),
                                      /*count=*/3, x, y);
  if (footer_col == 0) return State::TRANSFER;
  if (footer_col == 1) return State::SETTING;
  if (footer_col == 2) {
    ::ui.enterDeepSleep(/*from_reader=*/false);
    return State::BOOKSHELF;
  }

  if (prev_page_rect_.Contains(x, y)) {
    prevBookshelfPage();
    return State::BOOKSHELF;
  }
  if (next_page_rect_.Contains(x, y)) {
    nextBookshelfPage();
    return State::BOOKSHELF;
  }

  const int per_page = itemsPerPage(::ui.getCanvas());
  for (size_t i = 0; i < item_rects_.size(); ++i) {
    if (item_rects_[i].Contains(x, y)) {
      const size_t book_idx = page_index_ * per_page + i;
      if (book_idx < books.size()) {
        const String book_path = "/books/" + books[book_idx].filename;
        ::ui.openBook(book_path.c_str());
        return State::READER;
      }
    }
  }
  return State::BOOKSHELF;
}

void BookshelfPage::nextBookshelfPage() {
  const int per_page = itemsPerPage(::ui.getCanvas());
  if ((page_index_ + 1) * per_page <
      static_cast<int>(bookshelf_.getBookList().size())) {
    page_index_++;
    ::ui.drawCurrentPage();
  }
}

void BookshelfPage::prevBookshelfPage() {
  if (page_index_ > 0) {
    page_index_--;
    ::ui.drawCurrentPage();
  }
}

int BookshelfPage::itemsPerPage(const LGFX_Sprite& canvas) const {
  const int fs = ::ui.getFontSize();
  const int item_h = fs * 3 + 40;
  const int item_y = ::ui.getHeaderHeight() - 4 + 10;
  const int bottom_y =
      canvas.height() - ::ui.getFooterHeight() - static_cast<int>(fs * 1.5f);
  const int count = (bottom_y - item_y) / item_h;
  return std::max(1, count);
}

}  // namespace ui
}  // namespace reader
