#include "ui/pages/bookshelf_page.h"

#include <M5Unified.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "app/app_context.h"
#include "book/bookshelf_sort.h"
#include "book/epub_book.h"
#include "book/reading_records.h"
#include "core/perf_trace.h"
#include "hal/input.h"
#include "hal/storage.h"
#include "i18n/i18n.h"
#include "ui/text_renderer.h"
#include "ui/widgets/bars.h"

namespace reader {
namespace ui {

namespace {

// Dither-free 8-bit grayscale fixed points (`16 * k + 8`) for `Panel_IT8951`
// so 1px rules and secondary ink never stipple under M5GFX Bayer dithering.
constexpr uint8_t kInkPrimary = 0x00;
constexpr uint8_t kInkSecondary = 0x58;
constexpr uint8_t kRuleHairline = 0xB8;
constexpr uint8_t kTrackPale = 0xD8;

std::string FormatBookStatus(const BookInfo& book) {
  if (book.progress >= 100) {
    return book.isLastRead ? TR("上次读完") : TR("已读完");
  }
  if (book::HasBookBeenOpened(book)) {
    return std::to_string(book.progress) + "%";
  }
  return TR("未读");
}

std::string FormatBookMeta(const BookInfo& book) {
  const char* fmt = book::IsEpubPath(book.filename) ? "EPUB" : "TXT";
  char size_buf[32];
  if (book.size < 1024 * 1024) {
    const uint32_t kb =
        (book.size == 0) ? 0u : std::max<uint32_t>(1u, book.size / 1024);
    std::snprintf(size_buf, sizeof(size_buf), "%u KB", kb);
  } else {
    std::snprintf(size_buf, sizeof(size_buf), "%.1f MB",
                  book.size / (1024.0 * 1024.0));
  }
  if (book.isLastRead && book.progress < 100) {
    return std::string(TR("上次阅读 · ")) + fmt + " · " + size_buf;
  }
  return std::string(fmt) + " · " + size_buf;
}

}  // namespace

void BookshelfPage::enter(app::AppContext& ctx) {
  sort_menu_.Close();
  action_menu_.Close();
  confirm_dialog_.Close();
  selected_book_.clear();
  selected_title_.clear();
  uint32_t seq = ctx.settings.read_seq;
  bookshelf_.refresh(ctx.settings.bookshelf_sort, &seq);
  if (seq != ctx.settings.read_seq) {
    ctx.settings.read_seq = seq;
    ctx.SaveSettings();
  }
  clampPageIndex(ctx);
}

void BookshelfPage::clampPageIndex(const app::AppContext& ctx) {
  const int per_page = itemsPerPage(ctx, ctx.canvas());
  const int total = static_cast<int>(bookshelf_.getBookList().size());
  if (page_index_ * per_page >= total) {
    page_index_ = std::max(0, (total - 1) / per_page);
  }
}

void BookshelfPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  PERF_SCOPE("BookshelfPage::draw");
  UiPainter& p = ctx.painter();
  const std::vector<BookInfo>& books = bookshelf_.getBookList();
  const bool has_card = ctx.storage.card_present();
  const bool fallback_font = ctx.fonts.IsUsingFallbackBuiltin(&ctx.settings);

  std::string preload = i18n::GlyphPreload(
      "书架排序方式最近阅读书名阅读进度最近导入上次阅读上次读完已读完未读"
      "上一页下一页传书设置休眠取消<>·/%0123456789.MBKBEPUBTXT ");
  if (!has_card) {
    preload += TR("未检测到TF卡");
  } else if (books.empty()) {
    preload += TR("未找到书籍。");
  }
  if (fallback_font) {
    preload += TR("字体文件缺失，正在使用内置字体");
    preload += TR("请通过“传书”上传字体文件");
  }
  for (const BookInfo& book : books) {
    preload += book.title;
  }
  p.Prepare(preload);

  canvas.fillScreen(TFT_WHITE);

  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const int header_h = ctx.header_height();
  const int footer_h = ctx.footer_height();
  const int pager_h = fs * 2 + 3;
  const int pager_top = screen_h - footer_h - pager_h;

  // Unified 75px header: "书架" + book count + borderless sort trigger +
  // battery indicator.
  sort_rect_ = Rect{};
  const int header_text_y = p.CenterY(0, header_h);
  p.Text(TR("书架"), 20, header_text_y);
  if (has_card) {
    if (!books.empty()) {
      const std::string count_str = std::to_string(books.size());
      p.TextGray(count_str, 20 + p.Width(TR("书架")) + 10, header_text_y,
                 kInkSecondary);
    }
    const std::string sort_label =
        book::BookshelfSortName(ctx.settings.bookshelf_sort);
    constexpr int kTriWidth = 12;
    constexpr int kTriGap = 8;
    const int sort_total_w = p.Width(sort_label) + kTriGap + kTriWidth;
    const int sort_text_x = screen_w / 2 - sort_total_w / 2;
    p.Text(sort_label, sort_text_x, header_text_y);
    const int tri_x = sort_text_x + p.Width(sort_label) + kTriGap;
    const int tri_cy = header_text_y + fs / 2 + 1;
    canvas.fillTriangle(tri_x, tri_cy - 3, tri_x + kTriWidth, tri_cy - 3,
                        tri_x + kTriWidth / 2, tri_cy + 4, TFT_BLACK);
    sort_rect_ = Rect{screen_w / 2 - 100, 0, 200, header_h - 4};
  }
  ctx.DrawBattery(screen_w - 20, header_text_y);
  canvas.fillRect(0, header_h - 4, screen_w, 4, TFT_BLACK);

  const int item_h = fs * 4 + 2;
  const int per_page = itemsPerPage(ctx, canvas);
  const int total_books = static_cast<int>(books.size());
  const int total_pages = std::max(1, (total_books + per_page - 1) / per_page);
  const int start_idx = page_index_ * per_page;
  const int end_idx = std::min(total_books, start_idx + per_page);

  item_rects_.clear();
  int row_top = header_h;
  const int line_w = screen_w - 40;
  for (int i = start_idx; i < end_idx; ++i) {
    const BookInfo& book = books[static_cast<size_t>(i)];
    const Rect row{0, row_top, screen_w, item_h};
    item_rects_.push_back(row);

    const int title_y = row_top + 22;
    const int meta_y = row_top + 64;
    int text_x = 20;
    if (book.isLastRead) {
      canvas.fillCircle(25, title_y + fs / 2, 5, TFT_BLACK);
      text_x = 40;
    }

    const std::string right_str = FormatBookStatus(book);
    const int right_w = p.Width(right_str);
    const int max_title_w =
        std::max(fs * 2, screen_w - 20 - right_w - 16 - text_x);
    const std::string short_title =
        Ellipsize(p.cache(), book.title, max_title_w);
    p.Text(short_title, text_x, title_y);
    if (book.isLastRead) {
      p.Text(right_str, screen_w - 20 - right_w, title_y);
    } else {
      p.TextGray(right_str, screen_w - 20 - right_w, title_y, kInkSecondary);
    }

    const std::string meta_str = FormatBookMeta(book);
    p.TextGray(meta_str, text_x, meta_y, kInkSecondary);

    const int rule_y = row.bottom() - 1;
    if (book.progress >= 100) {
      canvas.drawFastHLine(
          20, rule_y, line_w,
          lgfx::color888(kRuleHairline, kRuleHairline, kRuleHairline));
    } else {
      canvas.drawFastHLine(
          20, rule_y, line_w,
          lgfx::color888(kTrackPale, kTrackPale, kTrackPale));
      if (book.progress > 0) {
        const int prog_w = std::max(4, line_w * book.progress / 100);
        const uint8_t fill_gray =
            book.isLastRead ? kInkPrimary : kInkSecondary;
        canvas.fillRect(20, rule_y - 1, prog_w, 2,
                        lgfx::color888(fill_gray, fill_gray, fill_gray));
      }
    }
    row_top += item_h;
  }

  if (has_card && books.empty()) {
    p.Text(TR("未找到书籍。"), 20, header_h + 24);
  }

  prev_page_rect_ = Rect{};
  next_page_rect_ = Rect{};
  if (total_pages > 1) {
    const int pager_text_y = p.CenterY(pager_top, pager_h);
    if (page_index_ > 0) {
      prev_page_rect_ = Rect{0, pager_top, screen_w / 3, pager_h};
      p.Text(TR("< 上一页"), 20, pager_text_y);
    }
    char page_buf[32];
    std::snprintf(page_buf, sizeof(page_buf), "%d / %d", page_index_ + 1,
                  total_pages);
    p.TextGray(page_buf, screen_w / 2 - p.Width(page_buf) / 2, pager_text_y,
               kInkSecondary);
    if (end_idx < total_books) {
      next_page_rect_ = Rect{screen_w * 2 / 3, pager_top, screen_w / 3,
                             pager_h};
      const std::string next_str = TR("下一页 >");
      p.Text(next_str, screen_w - 20 - p.Width(next_str), pager_text_y);
    }
  }

  ctx.DrawBottomBar({TR("传书"), TR("设置"), TR("休眠")});

  if (!has_card) {
    DrawMessageBox(p, {TR("未检测到TF卡")});
  } else if (confirm_dialog_.is_open()) {
    confirm_dialog_.Draw(p);
  } else if (sort_menu_.is_open()) {
    sort_menu_.Draw(p);
  } else if (action_menu_.is_open()) {
    action_menu_.Draw(p);
  } else if (fallback_font) {
    DrawMessageBox(
        p, {TR("字体文件缺失，正在使用内置字体"), TR("请通过“传书”上传字体文件")});
  }
}

void BookshelfPage::openDeleteConfirm(app::AppContext& ctx) {
  UiPainter& p = ctx.painter();
  const int fs = p.font_size();
  const int max_line_w =
      ctx.canvas().width() - 80 - 2 * static_cast<int>(fs * 0.8);
  const std::string& label =
      selected_title_.empty() ? selected_book_ : selected_title_;
  const std::string prefix = TR("确定删除《");
  const std::string suffix = TR("》？");
  p.Prepare(prefix + suffix + "..." + label);
  const int avail =
      std::max(fs * 2, max_line_w - p.Width(prefix) - p.Width(suffix));
  const std::string short_name = Ellipsize(p.cache(), label, avail);
  confirm_dialog_.Open(TR("删除书籍"),
                       {prefix + short_name + suffix,
                        TR("书籍文件和阅读记录都会删除，"), TR("此操作不可恢复。")},
                       TR("删除"));
  ctx.DrawCurrentPage();
}

void BookshelfPage::clearSelectedBookRecord(app::AppContext& ctx) {
  if (selected_book_.empty()) return;
  ctx.book.ReleaseIfPath("/books/" + selected_book_);
  if (book::ClearBookReadingRecord(selected_book_)) {
    bookshelf_.refresh(ctx.settings.bookshelf_sort);
    clampPageIndex(ctx);
    ctx.DrawCurrentPage();
  } else if (!hal::SdErrorPending()) {
    ctx.ShowPrompt(TR("操作失败"));
    delay(1000);
    ctx.DrawCurrentPage();
  }
}

void BookshelfPage::deleteSelectedBook(app::AppContext& ctx) {
  if (selected_book_.empty()) return;
  ctx.book.ReleaseIfPath("/books/" + selected_book_);
  if (book::DeleteBookFiles(selected_book_)) {
    selected_book_.clear();
    selected_title_.clear();
    bookshelf_.refresh(ctx.settings.bookshelf_sort);
    clampPageIndex(ctx);
    ctx.DrawCurrentPage();
  } else if (!hal::SdErrorPending()) {
    ctx.ShowPrompt(TR("删除失败"));
    delay(1000);
    bookshelf_.refresh(ctx.settings.bookshelf_sort);
    clampPageIndex(ctx);
    ctx.DrawCurrentPage();
  }
}

app::Nav BookshelfPage::handleEvent(const app::Event& msg,
                                    app::AppContext& ctx) {
  const std::vector<BookInfo>& books = bookshelf_.getBookList();

  if (confirm_dialog_.is_open()) {
    if (msg.type == app::EventType::kWheelPress) {
      confirm_dialog_.Close();
      ctx.DrawCurrentPage();
    } else if (msg.type == app::EventType::kTap) {
      switch (confirm_dialog_.Press(msg.tap.x, msg.tap.y)) {
        case Dialog::Result::kConfirm:
          confirm_dialog_.Close();
          deleteSelectedBook(ctx);
          break;
        case Dialog::Result::kCancel:
          confirm_dialog_.Close();
          ctx.DrawCurrentPage();
          break;
        case Dialog::Result::kNone:
          break;
      }
    }
    return app::Nav::Stay();
  }

  if (sort_menu_.is_open()) {
    if (msg.type == app::EventType::kWheelPress) {
      sort_menu_.Close();
      ctx.DrawCurrentPage();
    } else if (msg.type == app::EventType::kTap) {
      const int choice = sort_menu_.Press(msg.tap.x, msg.tap.y);
      if (choice >= 0 && choice < core::kBookshelfSortCount) {
        sort_menu_.Close();
        const auto mode = static_cast<core::BookshelfSort>(choice);
        if (ctx.settings.bookshelf_sort != mode) {
          ctx.settings.bookshelf_sort = mode;
          ctx.SaveSettings();
          bookshelf_.resort(mode);
          page_index_ = 0;
        }
        ctx.DrawCurrentPage();
      } else if (choice == ActionMenu::kCancel) {
        sort_menu_.Close();
        ctx.DrawCurrentPage();
      }
    }
    return app::Nav::Stay();
  }

  if (action_menu_.is_open()) {
    if (msg.type == app::EventType::kWheelPress) {
      action_menu_.Close();
      ctx.DrawCurrentPage();
    } else if (msg.type == app::EventType::kTap) {
      switch (action_menu_.Press(msg.tap.x, msg.tap.y)) {
        case 0:
          action_menu_.Close();
          clearSelectedBookRecord(ctx);
          break;
        case 1:
          action_menu_.Close();
          openDeleteConfirm(ctx);
          break;
        case ActionMenu::kCancel:
          action_menu_.Close();
          ctx.DrawCurrentPage();
          break;
        default:
          break;
      }
    }
    return app::Nav::Stay();
  }

  if (msg.type == app::EventType::kLongPress) {
    const int per_page = itemsPerPage(ctx, ctx.canvas());
    for (size_t i = 0; i < item_rects_.size(); ++i) {
      if (item_rects_[i].Contains(msg.tap.x, msg.tap.y)) {
        const size_t book_idx =
            static_cast<size_t>(page_index_ * per_page) + i;
        if (book_idx < books.size()) {
          selected_book_ = books[book_idx].filename;
          selected_title_ = books[book_idx].title;
          action_menu_.Open(selected_title_, {TR("清除阅读记录"), TR("删除书籍")});
          ctx.DrawCurrentPage();
        }
        break;
      }
    }
    return app::Nav::Stay();
  }

  if (msg.type == app::EventType::kWheel) {
    if (msg.wheel.delta > 0) {
      nextBookshelfPage(ctx);
      return app::Nav::Stay();
    }
    if (msg.wheel.delta < 0) {
      prevBookshelfPage(ctx);
      return app::Nav::Stay();
    }
  }

  if (msg.type == app::EventType::kSwipe) {
    const hal::Swipe swipe = hal::ClassifySwipe(msg.swipe.dx, msg.swipe.dy);
    if (swipe == hal::Swipe::kLeft || swipe == hal::Swipe::kUp) {
      nextBookshelfPage(ctx);
    } else if (swipe == hal::Swipe::kRight || swipe == hal::Swipe::kDown) {
      prevBookshelfPage(ctx);
    }
    return app::Nav::Stay();
  }
  if (msg.type == app::EventType::kWheelPress) {
    draw(ctx, ctx.canvas());
    ctx.frame.PushUiFullRefresh();
    return app::Nav::Stay();
  }
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();
  const int x = msg.tap.x;
  const int y = msg.tap.y;

  const int footer_col =
      BottomBarHit(ctx.canvas(), ctx.footer_height(), /*count=*/3, x, y);
  if (footer_col == 0) return app::Nav::Root(State::TRANSFER);
  if (footer_col == 1) return app::Nav::Root(State::SETTING);
  if (footer_col == 2) return app::Nav::Push(State::SLEEP);

  if (ctx.storage.card_present() && sort_rect_.Contains(x, y)) {
    sort_menu_.Open(TR("排序方式"), {TR("最近阅读"), TR("书名"), TR("阅读进度"), TR("最近导入")},
                    static_cast<int>(ctx.settings.bookshelf_sort));
    ctx.DrawCurrentPage();
    return app::Nav::Stay();
  }

  if (prev_page_rect_.Contains(x, y)) {
    prevBookshelfPage(ctx);
    return app::Nav::Stay();
  }
  if (next_page_rect_.Contains(x, y)) {
    nextBookshelfPage(ctx);
    return app::Nav::Stay();
  }

  const int per_page = itemsPerPage(ctx, ctx.canvas());
  for (size_t i = 0; i < item_rects_.size(); ++i) {
    if (item_rects_[i].Contains(x, y)) {
      const size_t book_idx = static_cast<size_t>(page_index_ * per_page) + i;
      if (book_idx < books.size()) {
        const std::string book_path = "/books/" + books[book_idx].filename;
        if (ctx.book.Open(book_path, &ctx.settings)) {
          return app::Nav::Root(State::READER);
        }
        if (!hal::SdErrorPending()) {
          ctx.ShowPrompt(TR("无法打开该书籍"));
          delay(1200);
          bookshelf_.refresh(ctx.settings.bookshelf_sort);
          ctx.DrawCurrentPage();
        }
        return app::Nav::Stay();
      }
    }
  }
  return app::Nav::Stay();
}

void BookshelfPage::nextBookshelfPage(app::AppContext& ctx) {
  const int per_page = itemsPerPage(ctx, ctx.canvas());
  if ((page_index_ + 1) * per_page <
      static_cast<int>(bookshelf_.getBookList().size())) {
    page_index_++;
    ctx.DrawCurrentPage();
  }
}

void BookshelfPage::prevBookshelfPage(app::AppContext& ctx) {
  if (page_index_ > 0) {
    page_index_--;
    ctx.DrawCurrentPage();
  }
}

int BookshelfPage::itemsPerPage(const app::AppContext& ctx,
                                const LGFX_Sprite& canvas) const {
  const int fs = ctx.font_size();
  const int item_h = fs * 4 + 2;
  const int pager_h = fs * 2 + 3;
  const int avail_h =
      canvas.height() - ctx.header_height() - ctx.footer_height() - pager_h;
  return std::max(1, avail_h / item_h);
}

}  // namespace ui
}  // namespace reader
