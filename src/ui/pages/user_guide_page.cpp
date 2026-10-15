#include "ui/pages/user_guide_page.h"

#include <M5Unified.h>

#include <cstdio>
#include <string>
#include <utility>

#include "book/page_layout.h"
#include "ui/widgets/bars.h"
#include "app/ui_manager.h"
#include "ui/pages/user_guide_text.h"

extern UIManager ui;

namespace reader {
namespace ui {

void UserGuidePage::enter() { current_page_idx_ = 0; }

void UserGuidePage::paginate(LGFX_Sprite& canvas) {
  pages_.clear();
  current_page_idx_ = 0;

  UiPainter& p = ::ui.painter();
  layout_font_path_ = p.font_path();
  const std::string text = USER_GUIDE_TEXT;
  p.Prepare(text);

  book::LayoutParams layout;
  layout.text_w = canvas.width() - 40;
  layout.text_h =
      canvas.height() - ::ui.getHeaderHeight() - ::ui.getFooterHeight() - 40;
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

void UserGuidePage::draw(LGFX_Sprite& canvas) {
  UiPainter& p = ::ui.painter();
  if (pages_.empty() || layout_font_path_ != p.font_path()) {
    paginate(canvas);
  }

  std::string preload = "返回使用说明0123456789/· ";
  if (current_page_idx_ >= 0 &&
      current_page_idx_ < static_cast<int>(pages_.size())) {
    preload += pages_[current_page_idx_];
  }
  p.Prepare(preload);

  canvas.fillScreen(TFT_WHITE);
  back_ = DrawBackHeader(p, ::ui.getHeaderHeight(),
                         M5.Power.getBatteryLevel());

  if (current_page_idx_ >= 0 &&
      current_page_idx_ < static_cast<int>(pages_.size())) {
    p.Text(pages_[current_page_idx_], 20, ::ui.getHeaderHeight() + 20);
  }

  char prog_str[64];
  snprintf(prog_str, sizeof(prog_str), "使用说明 · %d/%d",
           current_page_idx_ + 1, static_cast<int>(pages_.size()));
  ::ui.drawBottomBar({prog_str});
}

State UserGuidePage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type == MessageType::WHEEL) {
    if (msg.data.wheel.delta > 0 &&
        current_page_idx_ + 1 < static_cast<int>(pages_.size())) {
      current_page_idx_++;
      ::ui.drawCurrentPage();
    } else if (msg.data.wheel.delta < 0 && current_page_idx_ > 0) {
      current_page_idx_--;
      ::ui.drawCurrentPage();
    }
    return State::USER_GUIDE;
  }

  if (msg.type != MessageType::TOUCH) return State::USER_GUIDE;
  const int x = msg.data.touch.x;
  const int y = msg.data.touch.y;
  const LGFX_Sprite& canvas = ::ui.getCanvas();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();

  if (back_.Contains(x, y)) return State::SETTING;
  if (y >= ::ui.getHeaderHeight() && y < screen_h - ::ui.getFooterHeight()) {
    if (x < screen_w / 3 && current_page_idx_ > 0) {
      current_page_idx_--;
      ::ui.drawCurrentPage();
    } else if (x > 2 * screen_w / 3 &&
               current_page_idx_ + 1 < static_cast<int>(pages_.size())) {
      current_page_idx_++;
      ::ui.drawCurrentPage();
    }
  }
  return State::USER_GUIDE;
}

}  // namespace ui
}  // namespace reader
