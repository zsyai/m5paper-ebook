#include "ui/pages/reader_pages.h"

#include <M5Unified.h>

#include <algorithm>
#include <string>

#include "app/app_context.h"
#include "hal/input.h"
#include "i18n/i18n.h"
#include "ui/text_renderer.h"
#include "ui/widgets/controls.h"

namespace reader {
namespace ui {

namespace {

constexpr uint32_t kAutoTurnPromptMs = 800;
bool g_jump_from_toc = false;

int MenuRowHeight(const app::AppContext& ctx) {
  return static_cast<int>(ctx.font_size() * 2.5f);
}

}  // namespace

// ---- ReaderPage ------------------------------------------------------------

void ReaderPage::enter(app::AppContext& ctx) {
  const State from = get_current_state();
  force_full_refresh_ =
      from != State::READER_MENU &&
      (from != State::READER_JUMP_MENU || g_jump_from_toc);
  g_jump_from_toc = false;
  ctx.book.RefreshLayoutIfNeeded(&ctx.settings);
  restartAutoTurn();
}

void ReaderPage::exit(app::AppContext& ctx) { ctx.book.FlushConfig(); }

void ReaderPage::show(app::AppContext& ctx) {
  ctx.book.DrawReadingView(ctx.settings, force_full_refresh_);
  force_full_refresh_ = false;
}

void ReaderPage::restartAutoTurn() {
  last_turn_ms_ = millis();
  auto_turn_done_ = false;
}

app::Nav ReaderPage::runAction(app::AppContext& ctx, core::TouchAction action) {
  switch (action) {
    case core::TouchAction::kPrevPage:
      ctx.book.PrevPage(ctx.settings);
      break;
    case core::TouchAction::kNextPage:
      ctx.book.NextPage(ctx.settings);
      break;
    case core::TouchAction::kMenu:
      return app::Nav::Push(State::READER_MENU);
    case core::TouchAction::kFullRefresh:
      ctx.book.DrawReadingView(ctx.settings, /*force_full_refresh=*/true);
      break;
    case core::TouchAction::kToggleAutoTurn: {
      core::Settings& s = ctx.settings;
      s.auto_turn = !s.auto_turn;
      ctx.SaveSettings();
      restartAutoTurn();
      ctx.ShowPrompt(s.auto_turn
                         ? TR("自动翻页：开（") +
                               std::to_string(s.auto_turn_seconds) + TR("秒）")
                         : std::string(TR("自动翻页：关")));
      delay(kAutoTurnPromptMs);
      ctx.book.DrawReadingView(ctx.settings);
      break;
    }
    case core::TouchAction::kNone:
      break;
  }
  return app::Nav::Stay();
}

app::Nav ReaderPage::handleEvent(const app::Event& msg, app::AppContext& ctx) {
  const core::Settings& s = ctx.settings;
  if (msg.type == app::EventType::kTick) {
    ctx.book.OnReadingIdle(msg.timestamp_ms);
    autoTurnTick(ctx);
    return app::Nav::Stay();
  }
  if (msg.type == app::EventType::kIndexReady) return app::Nav::Stay();
  restartAutoTurn();
  if (msg.type == app::EventType::kWheel) {
    const int delta = s.wheel_inverted ? -msg.wheel.delta : msg.wheel.delta;
    if (delta > 0) {
      ctx.book.NextPage(s);
    } else if (delta < 0) {
      ctx.book.PrevPage(s);
    }
  } else if (msg.type == app::EventType::kTap) {
    const LGFX_Sprite& canvas = ctx.canvas();
    return runAction(ctx, s.TapAction(msg.tap.x, msg.tap.y, canvas.width(),
                                      canvas.height()));
  } else if (msg.type == app::EventType::kSwipe) {
    switch (hal::ClassifySwipe(msg.swipe.dx, msg.swipe.dy)) {
      case hal::Swipe::kLeft:
        return runAction(ctx, s.swipe_left);
      case hal::Swipe::kRight:
        return runAction(ctx, s.swipe_right);
      case hal::Swipe::kUp:
        return runAction(ctx, s.swipe_up);
      case hal::Swipe::kDown:
        return runAction(ctx, s.swipe_down);
      case hal::Swipe::kNone:
        break;
    }
  } else if (msg.type == app::EventType::kWheelPress) {
    ctx.book.DrawReadingView(s, /*force_full_refresh=*/true);
  }
  return app::Nav::Stay();
}

void ReaderPage::autoTurnTick(app::AppContext& ctx) {
  const core::Settings& s = ctx.settings;
  if (!s.auto_turn || auto_turn_done_) return;
  const uint32_t now = millis();
  if (now - last_turn_ms_ < static_cast<uint32_t>(s.auto_turn_seconds) * 1000) {
    return;
  }
  last_turn_ms_ = now;
  if (!ctx.book.NextPage(s)) auto_turn_done_ = true;
}

// ---- ReaderMenuPage --------------------------------------------------------

void ReaderMenuPage::enter(app::AppContext& /*ctx*/) {
  redraw_page_ = get_current_state() == State::READER_JUMP_MENU;
}

void ReaderMenuPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  UiPainter& p = ctx.painter();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const int fs = p.font_size();
  const int row_h = MenuRowHeight(ctx);
  const int half_w = screen_w / 2;

  top_bar_ = Rect{0, 0, screen_w, row_h * 2};
  canvas.fillRect(top_bar_.x, top_bar_.y, top_bar_.w, top_bar_.h, TFT_WHITE);
  back_ = Rect{0, 0, half_w, row_h};
  p.TextIn(TR("返回"), back_);
  canvas.drawFastVLine(half_w, 0, row_h, TFT_BLACK);
  ctx.DrawBattery(screen_w - 20, p.CenterY(0, row_h));
  canvas.drawFastHLine(0, row_h, screen_w, TFT_BLACK);
  const std::string title = ctx.book.Title();
  p.Prepare(title + "...");
  p.TextIn(Ellipsize(p.cache(), title, screen_w - 40),
           Rect{0, row_h, screen_w, row_h});
  canvas.fillRect(0, top_bar_.bottom() - 4, screen_w, 4, TFT_BLACK);

  const bool guide = ctx.book.IsUserGuide();
  const int button_h = ctx.footer_height();
  const int bottom_h = guide ? row_h : row_h + button_h;
  bottom_bar_ = Rect{0, screen_h - bottom_h, screen_w, bottom_h};
  canvas.fillRect(bottom_bar_.x, bottom_bar_.y, bottom_bar_.w, bottom_bar_.h,
                  TFT_WHITE);
  canvas.fillRect(0, bottom_bar_.y, screen_w, 4, TFT_BLACK);
  progress_ = Rect{0, bottom_bar_.y, screen_w, row_h};
  p.TextIn(ctx.book.ProgressText(), progress_);
  if (guide) {
    toc_ = settings_ = sleep_ = Rect{};
    return;
  }
  p.Prepare(i18n::GlyphPreload("目录阅读设置休眠"));
  DrawArrow(canvas, screen_w - 20 - fs / 2, progress_.center_y(),
            static_cast<int>(fs * 0.4f), /*right=*/true);
  canvas.drawFastHLine(0, progress_.bottom(), screen_w, TFT_BLACK);
  const int col1 = screen_w / 3;
  const int col2 = screen_w * 2 / 3;
  toc_ = Rect{0, progress_.bottom(), col1, button_h};
  settings_ = Rect{col1, progress_.bottom(), col2 - col1, button_h};
  sleep_ = Rect{col2, progress_.bottom(), screen_w - col2, button_h};
  p.TextIn(TR("目录"), toc_);
  p.TextIn(TR("阅读设置"), settings_);
  p.TextIn(TR("休眠"), sleep_);
  canvas.drawFastVLine(col1, toc_.y, button_h, TFT_BLACK);
  canvas.drawFastVLine(col2, toc_.y, button_h, TFT_BLACK);
}

void ReaderMenuPage::show(app::AppContext& ctx) {
  if (redraw_page_) {
    ctx.book.RenderCurrentPage();
    redraw_page_ = false;
  }
  draw(ctx, ctx.canvas());
  ctx.frame.PushReaderOverlay(/*quiet=*/false, ctx.settings.full_refresh_pages,
                              lgfx::epd_mode_t::epd_text);
}

app::Nav ReaderMenuPage::handleEvent(const app::Event& msg,
                                     app::AppContext& ctx) {
  if (msg.type == app::EventType::kIndexReady) {
    draw(ctx, ctx.canvas());
    ctx.frame.PushReaderOverlay(/*quiet=*/true, ctx.settings.full_refresh_pages,
                                lgfx::epd_mode_t::epd_text);
    return app::Nav::Stay();
  }
  if (msg.type == app::EventType::kWheelPress) {
    return app::Nav::Back();
  }
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();
  const int x = msg.tap.x;
  const int y = msg.tap.y;

  if (back_.Contains(x, y)) {
    return app::Nav::Root(State::BOOKSHELF);
  }
  if (top_bar_.Contains(x, y)) return app::Nav::Stay();
  if (bottom_bar_.Contains(x, y)) {
    if (ctx.book.IsUserGuide()) return app::Nav::Back();
    if (toc_.Contains(x, y)) {
      return app::Nav::Replace(State::READER_TOC);
    }
    if (settings_.Contains(x, y)) {
      return app::Nav::Replace(State::READING_SETTINGS);
    }
    if (sleep_.Contains(x, y)) {
      return app::Nav::Replace(State::SLEEP);
    }
    if (progress_.Contains(x, y)) {
      return app::Nav::Push(State::READER_JUMP_MENU);
    }
    return app::Nav::Stay();
  }
  return app::Nav::Back();
}

// ---- JumpPage --------------------------------------------------------------

namespace {

constexpr int kMaxPageDigits = 6;
constexpr int kMaxPercentDigits = 3;

}  // namespace

void JumpPage::enter(app::AppContext& ctx) {
  g_jump_from_toc = (get_current_state() == State::READER_TOC);
  keypad_.Open("", kMaxPageDigits, {TR("按页码"), TR("按百分比")}, kByPage);
  updateTitle(ctx);
}

void JumpPage::updateTitle(const app::AppContext& ctx) {
  keypad_.set_title(TR("跳转 ") + ctx.book.ProgressText());
}

void JumpPage::draw(app::AppContext& ctx, LGFX_Sprite& /*canvas*/) {
  updateTitle(ctx);
  keypad_.Draw(ctx.painter());
}

void JumpPage::show(app::AppContext& ctx) {
  draw(ctx, ctx.canvas());
  ctx.frame.PushReaderOverlay(/*quiet=*/false, ctx.settings.full_refresh_pages,
                              lgfx::epd_mode_t::epd_fast);
}

app::Nav JumpPage::handleEvent(const app::Event& msg, app::AppContext& ctx) {
  if (msg.type == app::EventType::kIndexReady) {
    draw(ctx, ctx.canvas());
    ctx.frame.PushReaderOverlay(/*quiet=*/true, ctx.settings.full_refresh_pages,
                                lgfx::epd_mode_t::epd_fast);
    return app::Nav::Stay();
  }
  if (msg.type == app::EventType::kWheelPress) {
    return app::Nav::Back();
  }
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();
  switch (keypad_.Press(msg.tap.x, msg.tap.y)) {
    case Keypad::Key::kMode:
      keypad_.set_max_digits(keypad_.mode() == kByPercent ? kMaxPercentDigits
                                                          : kMaxPageDigits);
      show(ctx);
      break;
    case Keypad::Key::kEdit:
      show(ctx);
      break;
    case Keypad::Key::kOk: {
      if (keypad_.text().empty()) break;
      const int value = std::stoi(keypad_.text());
      if (keypad_.mode() == kByPercent) {
        ctx.book.JumpToPercentage(static_cast<float>(std::min(value, 100)));
      } else {
        ctx.book.JumpToPage(std::max(value, 1) - 1);
      }
      return app::Nav::BackTo(State::READER);
    }
    case Keypad::Key::kCancel:
      return app::Nav::Back();
    case Keypad::Key::kNone:
      break;
  }
  return app::Nav::Stay();
}

}  // namespace ui
}  // namespace reader
