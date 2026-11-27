#include "ui/pages/toc_page.h"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app/app_context.h"
#include "book/book_toc.h"
#include "core/perf_trace.h"
#include "hal/input.h"
#include "i18n/i18n.h"
#include "ui/text_renderer.h"
#include "ui/widgets/bars.h"

namespace reader {
namespace ui {
namespace {

constexpr int kRowHeightPx = 80;
constexpr int kDotRadiusPx = 5;

}  // namespace

void TocPage::enter(app::AppContext& ctx) {
  ctx.book.EnsureTocReady();
  const int per_page = itemsPerPage(ctx, ctx.canvas());
  const int active = ctx.book.ActiveTocIndex();
  if (active >= 0) {
    page_index_ = active / per_page;
  } else {
    page_index_ = 0;
  }
  clampPageIndex(ctx);
}

int TocPage::itemsPerPage(const app::AppContext& ctx,
                          const LGFX_Sprite& canvas) const {
  const int fs = ctx.font_size();
  const int top = ctx.header_height();
  const int pager_h = static_cast<int>(fs * 1.5f);
  const int bottom = canvas.height() - ctx.footer_height() - pager_h;
  return std::max(1, (bottom - top) / kRowHeightPx);
}

void TocPage::clampPageIndex(const app::AppContext& ctx) {
  const int per_page = itemsPerPage(ctx, ctx.canvas());
  const int total = static_cast<int>(ctx.book.TocEntries().size());
  if (page_index_ * per_page >= total) {
    page_index_ = std::max(0, (total - 1) / per_page);
  }
}

void TocPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  PERF_SCOPE("TocPage::draw");
  UiPainter& p = ctx.painter();
  const std::vector<book::TocEntry>& entries = ctx.book.TocEntries();
  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const int header_h = ctx.header_height();
  const int footer_h = ctx.footer_height();
  const int pager_h = static_cast<int>(fs * 1.5f);
  const int pager_top = screen_h - footer_h - pager_h;

  const int per_page = itemsPerPage(ctx, canvas);
  const int total_entries = static_cast<int>(entries.size());
  const int total_pages =
      std::max(1, (total_entries + per_page - 1) / per_page);
  if (page_index_ >= total_pages) page_index_ = total_pages - 1;
  const int start_idx = page_index_ * per_page;
  const int end_idx = std::min(total_entries, start_idx + per_page);
  const int active_idx = ctx.book.ActiveTocIndex();

  std::string preload = i18n::GlyphPreload("目录暂无目录上一页下一页返回跳转…/0123456789.");
  for (int i = start_idx; i < end_idx; ++i) {
    preload += entries[static_cast<size_t>(i)].title;
  }
  p.Prepare(preload);

  canvas.fillScreen(TFT_WHITE);
  ctx.DrawHeader(TR("目录"));

  row_rects_.clear();
  prev_page_rect_ = Rect{};
  next_page_rect_ = Rect{};

  if (entries.empty()) {
    p.TextIn(TR("暂无目录"), Rect{0, header_h, screen_w, pager_top - header_h});
    ctx.DrawBottomBar({TR("返回"), TR("跳转")});
    return;
  }

  int top = header_h;
  for (int i = start_idx; i < end_idx; ++i) {
    const book::TocEntry& entry = entries[static_cast<size_t>(i)];
    const Rect row{0, top, screen_w, kRowHeightPx};
    row_rects_.push_back(row);

    const int indent =
        20 + std::min<int>(static_cast<int>(entry.level), 2) * fs;
    int text_x = indent;
    if (i == active_idx) {
      const int dot_cx = indent + kDotRadiusPx;
      canvas.fillCircle(dot_cx, row.center_y(), kDotRadiusPx, TFT_BLACK);
      text_x = indent + kDotRadiusPx * 2 + 10;
    }

    const int page_idx = ctx.book.PageForTocEntry(static_cast<size_t>(i));
    const std::string page_str =
        (page_idx >= 0) ? std::to_string(page_idx + 1) : std::string("…");
    const int page_w = p.Width(page_str);
    const int max_title_w = std::max(fs, screen_w - 20 - page_w - 20 - text_x);
    const std::string shown = Ellipsize(p.cache(), entry.title, max_title_w);

    const int text_y = p.CenterY(row.y, row.h);
    p.Text(shown, text_x, text_y);
    p.Text(page_str, screen_w - 20 - page_w, text_y);
    canvas.drawFastHLine(20, row.bottom(), screen_w - 40, TFT_BLACK);
    top += kRowHeightPx;
  }

  if (total_pages > 1) {
    const int pager_text_y = p.CenterY(pager_top, pager_h);
    if (page_index_ > 0) {
      prev_page_rect_ = Rect{0, pager_top, screen_w / 3, pager_h};
      p.Text(TR("上一页"), screen_w / 6 - p.Width(TR("上一页")) / 2, pager_text_y);
    }
    char counter[32];
    std::snprintf(counter, sizeof(counter), "%d/%d", page_index_ + 1,
                  total_pages);
    p.Text(counter, screen_w / 2 - p.Width(counter) / 2, pager_text_y);
    if (page_index_ + 1 < total_pages) {
      next_page_rect_ = Rect{screen_w * 2 / 3, pager_top, screen_w / 3,
                             pager_h};
      p.Text(TR("下一页"), screen_w * 5 / 6 - p.Width(TR("下一页")) / 2,
             pager_text_y);
    }
  }

  ctx.DrawBottomBar({TR("返回"), TR("跳转")});
}

app::Nav TocPage::handleEvent(const app::Event& msg, app::AppContext& ctx) {
  if (msg.type == app::EventType::kIndexReady) {
    ctx.book.EnsureTocReady();
    clampPageIndex(ctx);
    draw(ctx, ctx.canvas());
    ctx.frame.PushCanvas(lgfx::epd_mode_t::epd_text);
    return app::Nav::Stay();
  }
  if (msg.type == app::EventType::kWheelPress) {
    draw(ctx, ctx.canvas());
    ctx.frame.PushUiFullRefresh();
    return app::Nav::Stay();
  }
  if (msg.type == app::EventType::kWheel) {
    if (msg.wheel.delta > 0) nextTocPage(ctx);
    if (msg.wheel.delta < 0) prevTocPage(ctx);
    return app::Nav::Stay();
  }
  if (msg.type == app::EventType::kSwipe) {
    const hal::Swipe swipe = hal::ClassifySwipe(msg.swipe.dx, msg.swipe.dy);
    if (swipe == hal::Swipe::kLeft || swipe == hal::Swipe::kUp) {
      nextTocPage(ctx);
    } else if (swipe == hal::Swipe::kRight || swipe == hal::Swipe::kDown) {
      prevTocPage(ctx);
    }
    return app::Nav::Stay();
  }
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();

  const int x = msg.tap.x;
  const int y = msg.tap.y;
  const int footer_col =
      BottomBarHit(ctx.canvas(), ctx.footer_height(), /*count=*/2, x, y);
  if (footer_col == 0) return app::Nav::Back();
  if (footer_col == 1) return app::Nav::Push(State::READER_JUMP_MENU);

  if (prev_page_rect_.Contains(x, y)) {
    prevTocPage(ctx);
    return app::Nav::Stay();
  }
  if (next_page_rect_.Contains(x, y)) {
    nextTocPage(ctx);
    return app::Nav::Stay();
  }

  const int per_page = itemsPerPage(ctx, ctx.canvas());
  for (size_t i = 0; i < row_rects_.size(); ++i) {
    if (row_rects_[i].Contains(x, y)) {
      const size_t entry_idx =
          static_cast<size_t>(page_index_ * per_page) + i;
      if (ctx.book.JumpToTocEntry(entry_idx)) {
        return app::Nav::BackTo(State::READER);
      }
      ctx.DrawCurrentPage();
      return app::Nav::Stay();
    }
  }
  return app::Nav::Stay();
}

void TocPage::nextTocPage(app::AppContext& ctx) {
  const int per_page = itemsPerPage(ctx, ctx.canvas());
  if ((page_index_ + 1) * per_page <
      static_cast<int>(ctx.book.TocEntries().size())) {
    ++page_index_;
    ctx.DrawCurrentPage();
  }
}

void TocPage::prevTocPage(app::AppContext& ctx) {
  if (page_index_ > 0) {
    --page_index_;
    ctx.DrawCurrentPage();
  }
}

}  // namespace ui
}  // namespace reader
