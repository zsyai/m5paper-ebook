#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_CONTROLS_SETTINGS_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_CONTROLS_SETTINGS_PAGE_H_

// 操作设置 (second reading-settings page) and its TapZones / Swipe detail
// pages. Detail-page edits refresh only the changed rectangle
// (`AppContext::RedrawRegion`).

#include <string>

#include "core/settings.h"
#include "ui/page.h"
#include "ui/rect.h"
#include "ui/widgets/controls.h"
#include "ui/widgets/keypad.h"
#include "ui/widgets/setting_rows.h"

namespace reader {
namespace ui {

const char* TouchActionLabel(core::TouchAction action);
core::TouchAction CycleTouchAction(core::TouchAction action, int direction = 1);

class ControlsSettingsPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void exit(app::AppContext& ctx) override { keypad_.Close(); }
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  void show(app::AppContext& ctx) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

 private:
  enum class Field { kNone, kFullRefreshPages, kAutoTurnSeconds };

  void stepField(app::AppContext& ctx, Field field, int direction);
  void openKeypad(app::AppContext& ctx, Field field);
  void applyKeypad(app::AppContext& ctx);
  void commit(app::AppContext& ctx);

  Field editing_ = Field::kNone;
  Keypad keypad_;

  ValueStepper refresh_stepper_;
  Rect auto_toggle_;
  ValueStepper auto_stepper_;
  Rect tap_row_;
  Rect swipe_row_;
  SelectorRow wheel_row_;
  Rect bluetooth_row_;
};

class TapZonesPage : public Page {
 public:
  void enter(app::AppContext& ctx) override {}
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

 private:
  Rect zoneRect(core::TapLayout tap_layout, int row, int col) const;

  Segmented layout_;
  Rect layout_area_;
  Rect sketch_;
  Rect reset_;
};

class SwipeSettingsPage : public Page {
 public:
  void enter(app::AppContext& ctx) override {}
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

 private:
  SelectorRow rows_[4];
  Rect row_rects_[4];
  Rect reset_;
};

}  // namespace ui
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_CONTROLS_SETTINGS_PAGE_H_
