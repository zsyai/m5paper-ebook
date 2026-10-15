#include "ui/pages/reader_pages.h"

#include <algorithm>
#include <string>

#include "ui/text_renderer.h"
#include "ui/widgets/controls.h"
#include "ui_manager.h"

extern UIManager ui;

namespace reader {
namespace ui {

namespace {

// Height of one menu row, relative to the UI font size.
int MenuRowHeight() { return ::ui.getFontSize() * 2.5; }

}  // namespace

// ---- ReaderPage ------------------------------------------------------------

void ReaderPage::enter() {
  // Still the previous state while the new page is entered.
  const State from = get_current_state();
  force_full_refresh_ =
      from != State::READER_MENU && from != State::READER_JUMP_MENU;
  ::ui.refreshBookLayout();
}

void ReaderPage::show() {
  ::ui.drawReadingView(force_full_refresh_);
  force_full_refresh_ = false;
}

State ReaderPage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type == MessageType::WHEEL) {
    if (msg.data.wheel.delta > 0) {
      ::ui.nextPage();
    } else if (msg.data.wheel.delta < 0) {
      ::ui.prevPage();
    }
  } else if (msg.type == MessageType::TOUCH) {
    // Touch points are already in this page's (rotated) frame.
    const int x = msg.data.touch.x;
    const int screen_w = ::ui.getCanvas().width();
    if (x < screen_w / 3) {
      ::ui.prevPage();
    } else if (x > 2 * screen_w / 3) {
      ::ui.nextPage();
    } else {
      return State::READER_MENU;
    }
  } else if (msg.type == MessageType::SYSTEM && msg.data.sys_code == 1) {
    ::ui.drawReadingView(/*forceFullRefresh=*/true);  // Wheel press.
  }
  return State::READER;
}

// ---- ReaderMenuPage --------------------------------------------------------

void ReaderMenuPage::enter() {
  redraw_page_ = get_current_state() == State::READER_JUMP_MENU;
}

void ReaderMenuPage::draw(LGFX_Sprite& canvas) {
  UiPainter& p = ::ui.painter();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const int fs = p.font_size();
  const int row_h = MenuRowHeight();
  const int half_w = screen_w / 2;

  // Top bar: 返回 | battery, then the file name.
  top_bar_ = Rect{0, 0, screen_w, row_h * 2};
  canvas.fillRect(top_bar_.x, top_bar_.y, top_bar_.w, top_bar_.h, TFT_WHITE);
  back_ = Rect{0, 0, half_w, row_h};
  p.TextIn("返回", back_);
  canvas.drawFastVLine(half_w, 0, row_h, TFT_BLACK);
  ::ui.drawBattery(screen_w - 20, p.CenterY(0, row_h));
  canvas.drawFastHLine(0, row_h, screen_w, TFT_BLACK);
  const std::string title = ::ui.bookTitle();
  p.Prepare(title + "...");
  p.TextIn(Ellipsize(p.cache(), title, screen_w - 40),
           Rect{0, row_h, screen_w, row_h});
  canvas.fillRect(0, top_bar_.bottom() - 4, screen_w, 4, TFT_BLACK);

  // Bottom bar.
  // The button row is as tall as the other pages' bottom bars.
  const bool guide = ::ui.isReadingUserGuide();
  const int button_h = ::ui.getFooterHeight();
  const int bottom_h = guide ? row_h : row_h + button_h;
  bottom_bar_ = Rect{0, screen_h - bottom_h, screen_w, bottom_h};
  canvas.fillRect(bottom_bar_.x, bottom_bar_.y, bottom_bar_.w, bottom_bar_.h,
                  TFT_WHITE);
  canvas.fillRect(0, bottom_bar_.y, screen_w, 4, TFT_BLACK);
  progress_ = Rect{0, bottom_bar_.y, screen_w, row_h};
  p.TextIn(::ui.progressText(), progress_);
  if (guide) {
    settings_ = sleep_ = Rect{};
    return;
  }
  DrawArrow(canvas, screen_w - 20 - fs / 2, progress_.center_y(), fs * 0.4,
            /*right=*/true);
  canvas.drawFastHLine(0, progress_.bottom(), screen_w, TFT_BLACK);
  settings_ = Rect{0, progress_.bottom(), half_w, button_h};
  sleep_ = Rect{half_w, progress_.bottom(), screen_w - half_w, button_h};
  p.TextIn("阅读设置", settings_);
  p.TextIn("休眠", sleep_);
  canvas.drawFastVLine(half_w, settings_.y, button_h, TFT_BLACK);
}

void ReaderMenuPage::show() {
  if (redraw_page_) {
    ::ui.renderReadingView();
    redraw_page_ = false;
  }
  draw(::ui.getCanvas());
  ::ui.pushReaderOverlay(/*quiet=*/false, lgfx::epd_mode_t::epd_text);
}

void ReaderMenuPage::onIndexReady() {
  draw(::ui.getCanvas());
  ::ui.pushReaderOverlay(/*quiet=*/true, lgfx::epd_mode_t::epd_text);
}

State ReaderMenuPage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type == MessageType::SYSTEM && msg.data.sys_code == 1) {
    return State::READER;  // Wheel press closes the menu.
  }
  if (msg.type != MessageType::TOUCH) return State::READER_MENU;
  const int x = msg.data.touch.x;
  const int y = msg.data.touch.y;

  if (back_.Contains(x, y)) {
    ::ui.setSelection(0);
    return State::BOOKSHELF;
  }
  if (top_bar_.Contains(x, y)) return State::READER_MENU;
  if (bottom_bar_.Contains(x, y)) {
    if (::ui.isReadingUserGuide()) return State::READER;
    if (settings_.Contains(x, y)) return State::READING_SETTINGS;
    if (sleep_.Contains(x, y)) ::ui.enterDeepSleep(/*from_reader=*/true);
    if (progress_.Contains(x, y)) return State::READER_JUMP_MENU;
    return State::READER_MENU;
  }
  return State::READER;  // Tap on the page closes the menu.
}

// ---- JumpPage --------------------------------------------------------------

namespace {

constexpr int kMaxPageDigits = 6;
constexpr int kMaxPercentDigits = 3;

}  // namespace

void JumpPage::enter() {
  keypad_.Open("", kMaxPageDigits, {"按页码", "按百分比"}, kByPage);
  updateTitle();
}

void JumpPage::updateTitle() {
  keypad_.set_title("跳转 " + ::ui.progressText());
}

void JumpPage::draw(LGFX_Sprite& canvas) {
  updateTitle();
  keypad_.Draw(::ui.painter());
}

void JumpPage::show() {
  draw(::ui.getCanvas());
  ::ui.pushReaderOverlay(/*quiet=*/false, lgfx::epd_mode_t::epd_fast);
}

void JumpPage::onIndexReady() {
  draw(::ui.getCanvas());
  ::ui.pushReaderOverlay(/*quiet=*/true, lgfx::epd_mode_t::epd_fast);
}

State JumpPage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type == MessageType::SYSTEM && msg.data.sys_code == 1) {
    return State::READER_MENU;  // Wheel press cancels, like 取消.
  }
  if (msg.type != MessageType::TOUCH) return State::READER_JUMP_MENU;
  switch (keypad_.Press(msg.data.touch.x, msg.data.touch.y)) {
    case Keypad::Key::kMode:
      keypad_.set_max_digits(keypad_.mode() == kByPercent ? kMaxPercentDigits
                                                          : kMaxPageDigits);
      show();
      break;
    case Keypad::Key::kEdit:
      show();
      break;
    case Keypad::Key::kOk: {
      if (keypad_.text().empty()) break;
      const int value = std::stoi(keypad_.text());
      if (keypad_.mode() == kByPercent) {
        ::ui.jumpToPercentage(std::min(value, 100));
      } else {
        ::ui.jumpToPage(std::max(value, 1) - 1);
      }
      return State::READER;
    }
    case Keypad::Key::kCancel:
      return State::READER_MENU;
    case Keypad::Key::kNone:
      break;
  }
  return State::READER_JUMP_MENU;
}

}  // namespace ui
}  // namespace reader
