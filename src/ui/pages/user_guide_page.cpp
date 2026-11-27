#include "ui/pages/user_guide_page.h"

#include <M5Unified.h>

#include <cstdio>
#include <string>
#include <utility>

#include "app/app_context.h"
#include "book/page_layout.h"
#include "hal/input.h"
#include "i18n/i18n.h"
#include "ui/widgets/bars.h"

namespace reader {
namespace ui {

void UserGuidePage::enter(app::AppContext& /*ctx*/) { current_page_idx_ = 0; }

void UserGuidePage::paginate(app::AppContext& ctx, LGFX_Sprite& canvas) {
  pages_.clear();
  current_page_idx_ = 0;

  UiPainter& p = ctx.painter();
  layout_font_path_ = p.font_path();
  layout_language_ = i18n::CurrentLanguage();
  const std::string text = i18n::UserGuideText();
  p.Prepare(text);

  book::LayoutParams layout;
  layout.text_w = canvas.width() - 40;
  layout.text_h =
      canvas.height() - ctx.header_height() - ctx.footer_height() - 40;
  layout.font_size = p.font_size();
  layout.line_height = static_cast<int>(layout.font_size * 1.5f);
  layout.paragraph_indent = false;

  const auto* raw = reinterpret_cast<const uint8_t*>(text.data());
  const size_t len = text.size();
  size_t offset = 0;
  while (offset < len) {
    book::LayoutCursor cursor;
    bool filled = false;
    std::string page_text;
    const size_t used =
        book::MeasurePage(raw + offset, len - offset, layout, p.cache(),
                          &cursor, &filled, &page_text);
    if (used == 0) break;
    offset += used;
    pages_.push_back(std::move(page_text));
  }
}

void UserGuidePage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  UiPainter& p = ctx.painter();
  if (pages_.empty() || layout_font_path_ != p.font_path() ||
      layout_language_ != i18n::CurrentLanguage()) {
    paginate(ctx, canvas);
  }

  std::string preload = i18n::GlyphPreload("返回使用说明0123456789/· ");
  if (current_page_idx_ >= 0 &&
      current_page_idx_ < static_cast<int>(pages_.size())) {
    preload += pages_[current_page_idx_];
  }
  p.Prepare(preload);

  canvas.fillScreen(TFT_WHITE);
  back_ = DrawBackHeader(p, ctx.header_height(), ctx.power.BatteryLevel());

  if (current_page_idx_ >= 0 &&
      current_page_idx_ < static_cast<int>(pages_.size())) {
    p.Text(pages_[current_page_idx_], 20, ctx.header_height() + 20);
  }

  char prog_str[64];
  std::snprintf(prog_str, sizeof(prog_str), TR("使用说明 · %d/%d"),
                current_page_idx_ + 1, static_cast<int>(pages_.size()));
  ctx.DrawBottomBar({prog_str});
}

app::Nav UserGuidePage::handleEvent(const app::Event& msg,
                                    app::AppContext& ctx) {
  if (msg.type == app::EventType::kWheel) {
    if (msg.wheel.delta > 0 &&
        current_page_idx_ + 1 < static_cast<int>(pages_.size())) {
      current_page_idx_++;
      ctx.DrawCurrentPage();
    } else if (msg.wheel.delta < 0 && current_page_idx_ > 0) {
      current_page_idx_--;
      ctx.DrawCurrentPage();
    }
    return app::Nav::Stay();
  }

  if (msg.type == app::EventType::kSwipe) {
    const hal::Swipe swipe = hal::ClassifySwipe(msg.swipe.dx, msg.swipe.dy);
    if (swipe == hal::Swipe::kLeft &&
        current_page_idx_ + 1 < static_cast<int>(pages_.size())) {
      current_page_idx_++;
      ctx.DrawCurrentPage();
    } else if (swipe == hal::Swipe::kRight && current_page_idx_ > 0) {
      current_page_idx_--;
      ctx.DrawCurrentPage();
    }
    return app::Nav::Stay();
  }
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();
  const int x = msg.tap.x;
  const int y = msg.tap.y;
  const LGFX_Sprite& canvas = ctx.canvas();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();

  if (back_.Contains(x, y)) return app::Nav::Back();
  if (y >= ctx.header_height() && y < screen_h - ctx.footer_height()) {
    if (x < screen_w / 3 && current_page_idx_ > 0) {
      current_page_idx_--;
      ctx.DrawCurrentPage();
    } else if (x > 2 * screen_w / 3 &&
               current_page_idx_ + 1 < static_cast<int>(pages_.size())) {
      current_page_idx_++;
      ctx.DrawCurrentPage();
    }
  }
  return app::Nav::Stay();
}

}  // namespace ui
}  // namespace reader
