#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_READER_PAGES_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_READER_PAGES_H_

// Pages shown while a book is open: the reading view, the menu drawn over
// it, and the jump keypad drawn over the menu.

#include <cstdint>

#include "core/settings.h"
#include "ui/page.h"
#include "ui/rect.h"
#include "ui/widgets/keypad.h"

namespace reader {
namespace ui {

class ReaderPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void exit(app::AppContext& ctx) override;
  // Drawn and pushed by `BookSession::DrawReadingView`.
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override {}
  void show(app::AppContext& ctx) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;
  bool followsReadingDirection() const override { return true; }

 private:
  app::Nav runAction(app::AppContext& ctx, core::TouchAction action);
  void restartAutoTurn();
  void autoTurnTick(app::AppContext& ctx);

  bool force_full_refresh_ = false;
  uint32_t last_turn_ms_ = 0;
  bool auto_turn_done_ = false;
};

class ReaderMenuPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  void show(app::AppContext& ctx) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;
  bool followsReadingDirection() const override { return true; }

 private:
  Rect back_;
  Rect top_bar_;
  Rect progress_;
  Rect toc_;
  Rect settings_;
  Rect sleep_;
  Rect bottom_bar_;
  bool redraw_page_ = false;
};

class JumpPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void exit(app::AppContext& ctx) override { keypad_.Close(); }
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  void show(app::AppContext& ctx) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

 private:
  enum Mode { kByPage = 0, kByPercent = 1 };
  void updateTitle(const app::AppContext& ctx);

  Keypad keypad_;
};

}  // namespace ui
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_READER_PAGES_H_
