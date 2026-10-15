#pragma once

// Pages shown while a book is open: the reading view, the menu drawn over
// it, and the jump keypad drawn over the menu. They replace the special
// cases that used to live in state_machine.cpp.

#include "ui/page.h"
#include "ui/rect.h"
#include "ui/widgets/keypad.h"

namespace reader {
namespace ui {

class ReaderPage : public Page {
 public:
  // Re-lays out the current page if reading settings changed, and asks for
  // a full refresh unless coming back from the menu or jump overlays.
  void enter() override;
  void exit() override {}
  // The reading view is drawn and pushed by UIManager::drawReadingView().
  void draw(LGFX_Sprite& canvas) override {}
  void show() override;
  State handleEvent(const SystemMessage_t& msg) override;
  bool followsReadingDirection() const override { return true; }

 private:
  bool force_full_refresh_ = false;
};

// Top: 返回 | battery, then the file name. Bottom: the progress row (opens
// the jump keypad), then 设置 | 休眠. While reading the user guide the
// bottom shows only the progress.
class ReaderMenuPage : public Page {
 public:
  // Coming back from the jump keypad, the page under it is redrawn first.
  void enter() override;
  void exit() override {}
  void draw(LGFX_Sprite& canvas) override;
  void show() override;
  void onIndexReady() override;
  State handleEvent(const SystemMessage_t& msg) override;
  bool followsReadingDirection() const override { return true; }

 private:
  Rect back_;
  Rect top_bar_;
  Rect progress_;
  Rect settings_;
  Rect sleep_;
  Rect bottom_bar_;
  bool redraw_page_ = false;
};

// Keypad over the menu. 按页码 jumps to a page number, 按百分比 to a
// percentage of the book. 确定 jumps and returns to the page; 取消 returns
// to the menu. It is always drawn in portrait, over the page as it is.
class JumpPage : public Page {
 public:
  void enter() override;
  void exit() override { keypad_.Close(); }
  void draw(LGFX_Sprite& canvas) override;
  void show() override;
  void onIndexReady() override;
  State handleEvent(const SystemMessage_t& msg) override;

 private:
  enum Mode { kByPage = 0, kByPercent = 1 };
  void updateTitle();

  Keypad keypad_;
};

}  // namespace ui
}  // namespace reader
