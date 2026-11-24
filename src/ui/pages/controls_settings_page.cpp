#include "ui/pages/controls_settings_page.h"

#include <M5Unified.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app/app_context.h"
#include "core/perf_trace.h"
#include "ui/widgets/bars.h"

namespace reader {
namespace ui {

namespace {

const std::vector<int> kAutoTurnPresets = {5,  10, 15,  20,  30,  45,
                                           60, 90, 120, 180, 300, 600};
constexpr int kMaxRefreshDigits = 2;
constexpr int kMaxSecondsDigits = 3;

std::string RangeTitle(const char* name, int lo, int hi) {
  return std::string(name) + " " + std::to_string(lo) + "-" +
         std::to_string(hi);
}

int RowHeight(const app::AppContext& ctx) {
  return static_cast<int>(ctx.font_size() * 2.5f);
}

void DrawNavRow(UiPainter& p, const Rect& row, const std::string& label,
                const std::string& value) {
  DrawRowLabel(p, row, label);
  const int fs = p.font_size();
  const int arrow_x = row.right() - kRowPaddingX - fs / 2;
  DrawArrow(p.canvas(), arrow_x, row.center_y(), static_cast<int>(fs * 0.4f),
            /*right=*/true);
  if (!value.empty()) {
    p.Prepare(value);
    p.Text(value, arrow_x - fs - p.Width(value), p.CenterY(row.y, row.h));
  }
}

Rect DrawResetButton(UiPainter& p, int top) {
  const int fs = p.font_size();
  const int w = fs * 5;
  const int h = static_cast<int>(fs * 1.8f);
  const Rect r{(p.canvas().width() - w) / 2, top, w, h};
  DrawButton(p, r, "恢复默认");
  return r;
}

void DashedV(LGFX_Sprite& c, int x, int y0, int y1) {
  for (int y = y0; y < y1; y += 12) {
    c.drawFastVLine(x, y, std::min(6, y1 - y), TFT_BLACK);
    c.drawFastVLine(x + 1, y, std::min(6, y1 - y), TFT_BLACK);
  }
}

void DashedH(LGFX_Sprite& c, int x0, int x1, int y) {
  for (int x = x0; x < x1; x += 12) {
    c.drawFastHLine(x, y, std::min(6, x1 - x), TFT_BLACK);
    c.drawFastHLine(x, y + 1, std::min(6, x1 - x), TFT_BLACK);
  }
}

}  // namespace

const char* TouchActionLabel(core::TouchAction action) {
  switch (action) {
    case core::TouchAction::kPrevPage:
      return "上一页";
    case core::TouchAction::kNextPage:
      return "下一页";
    case core::TouchAction::kMenu:
      return "菜单";
    case core::TouchAction::kFullRefresh:
      return "全刷";
    case core::TouchAction::kToggleAutoTurn:
      return "自动翻页";
    case core::TouchAction::kNone:
      break;
  }
  return "无";
}

core::TouchAction CycleTouchAction(core::TouchAction action, int direction) {
  const int n = core::kTouchActionCount;
  const int next = (static_cast<int>(action) + (direction < 0 ? n - 1 : 1)) % n;
  return static_cast<core::TouchAction>(next);
}

// ---- ControlsSettingsPage --------------------------------------------------

void ControlsSettingsPage::enter(app::AppContext& /*ctx*/) {
  editing_ = Field::kNone;
  keypad_.Close();
}

void ControlsSettingsPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  PERF_SCOPE("ControlsSettingsPage::draw");
  UiPainter& p = ctx.painter();
  const core::Settings& s = ctx.settings;
  canvas.fillScreen(TFT_WHITE);
  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int row_h = RowHeight(ctx);
  ctx.DrawHeader("操作设置");

  int top = ctx.header_height() - 4;
  auto next_row = [&]() {
    const Rect row{0, top, screen_w, row_h};
    top += row_h;
    return row;
  };

  const Rect refresh_row = next_row();
  DrawRowLabel(p, refresh_row, "全刷间隔");
  refresh_stepper_.Draw(p, refresh_row,
                        std::to_string(s.full_refresh_pages) + " 页");

  const Rect auto_row = next_row();
  DrawRowLabel(p, auto_row, "");
  const std::string auto_label = "自动翻页";
  p.Prepare(auto_label);
  p.Text(auto_label, kRowPaddingX, p.CenterY(auto_row.y, auto_row.h));
  const int toggle_h = static_cast<int>(fs * 1.8f);
  const Rect toggle{kRowPaddingX + p.Width(auto_label) + fs / 2,
                    auto_row.center_y() - toggle_h / 2,
                    static_cast<int>(fs * 2.2f), toggle_h};
  DrawToggle(p, toggle, s.auto_turn);
  auto_toggle_ = Rect{0, auto_row.y, toggle.x + toggle.w, auto_row.h};
  auto_stepper_.Draw(p, auto_row, std::to_string(s.auto_turn_seconds) + "秒");

  tap_row_ = next_row();
  DrawNavRow(p, tap_row_, "点击区域",
             s.tap_layout == core::TapLayout::kGrid ? "九宫格" : "三栏");
  swipe_row_ = next_row();
  DrawNavRow(p, swipe_row_, "滑动操作", "");
  wheel_row_.Draw(p, next_row(), "滚轮方向", s.wheel_inverted ? "反转" : "正常",
                  /*has_arrows=*/true);
  bluetooth_row_ = next_row();
  DrawNavRow(p, bluetooth_row_, "蓝牙翻页器", "未连接");

  ctx.DrawBottomBar({"返回", "排版设置"});
  keypad_.Draw(p);
}

void ControlsSettingsPage::show(app::AppContext& ctx) {
  draw(ctx, ctx.canvas());
  if (keypad_.is_open()) {
    ctx.frame.PushCanvas(lgfx::epd_mode_t::epd_fast);
  } else {
    ctx.frame.PushUiFrame();
  }
}

void ControlsSettingsPage::commit(app::AppContext& ctx) {
  ctx.SaveSettings();
  ctx.DrawCurrentPage();
}

void ControlsSettingsPage::stepField(app::AppContext& ctx, Field field,
                                     int direction) {
  core::Settings& s = ctx.settings;
  switch (field) {
    case Field::kFullRefreshPages:
      ctx.SetFullRefreshPages(
          StepLinear(s.full_refresh_pages, 1, core::kMinFullRefreshPages,
                     core::kMaxFullRefreshPages, direction));
      break;
    case Field::kAutoTurnSeconds:
      s.auto_turn_seconds =
          StepPreset(s.auto_turn_seconds, kAutoTurnPresets, direction);
      break;
    case Field::kNone:
      return;
  }
  commit(ctx);
}

void ControlsSettingsPage::openKeypad(app::AppContext& ctx, Field field) {
  editing_ = field;
  switch (field) {
    case Field::kFullRefreshPages:
      keypad_.Open(RangeTitle("全刷间隔", core::kMinFullRefreshPages,
                              core::kMaxFullRefreshPages),
                   kMaxRefreshDigits);
      break;
    case Field::kAutoTurnSeconds:
      keypad_.Open(RangeTitle("自动翻页秒数", core::kMinAutoTurnSeconds,
                              core::kMaxAutoTurnSeconds),
                   kMaxSecondsDigits);
      break;
    case Field::kNone:
      return;
  }
  ctx.DrawCurrentPage();
}

void ControlsSettingsPage::applyKeypad(app::AppContext& ctx) {
  if (keypad_.text().empty()) return;
  const int value = std::stoi(keypad_.text());
  switch (editing_) {
    case Field::kFullRefreshPages:
      ctx.SetFullRefreshPages(value);
      break;
    case Field::kAutoTurnSeconds:
      ctx.settings.auto_turn_seconds = core::ClampAutoTurnSeconds(value);
      break;
    case Field::kNone:
      break;
  }
}

app::Nav ControlsSettingsPage::handleEvent(const app::Event& msg,
                                           app::AppContext& ctx) {
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();
  const int x = msg.tap.x;
  const int y = msg.tap.y;

  if (keypad_.is_open()) {
    switch (keypad_.Press(x, y)) {
      case Keypad::Key::kEdit:
      case Keypad::Key::kMode:
        ctx.DrawCurrentPage();
        break;
      case Keypad::Key::kOk:
        applyKeypad(ctx);
        keypad_.Close();
        editing_ = Field::kNone;
        commit(ctx);
        break;
      case Keypad::Key::kCancel:
        keypad_.Close();
        editing_ = Field::kNone;
        ctx.DrawCurrentPage();
        break;
      case Keypad::Key::kNone:
        break;
    }
    return app::Nav::Stay();
  }

  switch (BottomBarHit(ctx.canvas(), ctx.footer_height(), 2, x, y)) {
    case 0:
      return app::Nav::Back();
    case 1:
      return app::Nav::Replace(State::READING_SETTINGS);
    default:
      break;
  }

  core::Settings& s = ctx.settings;
  const struct {
    const ValueStepper& stepper;
    Field field;
  } steppers[] = {
      {refresh_stepper_, Field::kFullRefreshPages},
      {auto_stepper_, Field::kAutoTurnSeconds},
  };
  for (const auto& st : steppers) {
    switch (st.stepper.HitTest(x, y)) {
      case ValueStepper::Hit::kDecrement:
        stepField(ctx, st.field, -1);
        return app::Nav::Stay();
      case ValueStepper::Hit::kIncrement:
        stepField(ctx, st.field, +1);
        return app::Nav::Stay();
      case ValueStepper::Hit::kValue:
        openKeypad(ctx, st.field);
        return app::Nav::Stay();
      case ValueStepper::Hit::kNone:
        break;
    }
  }
  if (auto_toggle_.Contains(x, y)) {
    s.auto_turn = !s.auto_turn;
    commit(ctx);
  } else if (tap_row_.Contains(x, y)) {
    return app::Nav::Push(State::TAP_ZONES);
  } else if (swipe_row_.Contains(x, y)) {
    return app::Nav::Push(State::SWIPE_SETTINGS);
  } else if (wheel_row_.HitTest(x, y) != 0) {
    s.wheel_inverted = !s.wheel_inverted;
    commit(ctx);
  } else if (bluetooth_row_.Contains(x, y)) {
    ctx.ShowPrompt("蓝牙翻页器暂未支持");
    delay(1200);
    ctx.DrawCurrentPage();
  }
  return app::Nav::Stay();
}

// ---- TapZonesPage ----------------------------------------------------------

void TapZonesPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  UiPainter& p = ctx.painter();
  const core::Settings& s = ctx.settings;
  canvas.fillScreen(TFT_WHITE);
  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int footer_top = canvas.height() - ctx.footer_height();
  ctx.DrawHeader("点击区域");

  int top = ctx.header_height() - 4;
  const int row_h = RowHeight(ctx);
  const int seg_h = static_cast<int>(fs * 1.8f);
  layout_area_ = Rect{kRowPaddingX, top + (row_h - seg_h) / 2,
                      screen_w - 2 * kRowPaddingX, seg_h};
  layout_.Draw(p, layout_area_, {"三栏", "九宫格"},
               static_cast<int>(s.tap_layout));
  top += row_h;
  p.TextIn("点区域切换功能", Rect{0, top, screen_w, fs * 2});
  top += fs * 2;

  const int button_h = static_cast<int>(fs * 1.8f);
  const int bottom = footer_top - button_h - fs * 2;
  const int avail_w = screen_w - 2 * 60;
  const int avail_h = bottom - top;
  const bool landscape = static_cast<int>(s.orientation) & 1;
  const float aspect = landscape ? 960.0f / 540.0f : 540.0f / 960.0f;
  int sw = avail_w;
  int sh = static_cast<int>(sw / aspect);
  if (sh > avail_h) {
    sh = avail_h;
    sw = static_cast<int>(sh * aspect);
  }
  sketch_ = Rect{(screen_w - sw) / 2, top + (avail_h - sh) / 2, sw, sh};
  canvas.drawRect(sketch_.x, sketch_.y, sw, sh, TFT_BLACK);
  canvas.drawRect(sketch_.x + 1, sketch_.y + 1, sw - 2, sh - 2, TFT_BLACK);
  const bool grid = s.tap_layout == core::TapLayout::kGrid;
  const int rows = grid ? 3 : 1;
  for (int i = 1; i < 3; ++i) {
    DashedV(canvas, sketch_.x + sw * i / 3, sketch_.y, sketch_.bottom());
    if (grid) {
      DashedH(canvas, sketch_.x, sketch_.right(), sketch_.y + sh * i / 3);
    }
  }
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < 3; ++c) {
      const core::TouchAction a =
          grid ? s.tap_grid[r * 3 + c] : s.tap_columns[c];
      p.TextIn(TouchActionLabel(a), zoneRect(s.tap_layout, r, c));
    }
  }

  reset_ = DrawResetButton(p, footer_top - button_h - fs);
  ctx.DrawBottomBar({"返回"});
}

Rect TapZonesPage::zoneRect(core::TapLayout tap_layout, int row,
                            int col) const {
  const int rows = tap_layout == core::TapLayout::kGrid ? 3 : 1;
  const int x0 = sketch_.x + sketch_.w * col / 3;
  const int x1 = sketch_.x + sketch_.w * (col + 1) / 3;
  const int y0 = sketch_.y + sketch_.h * row / rows;
  const int y1 = sketch_.y + sketch_.h * (row + 1) / rows;
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

app::Nav TapZonesPage::handleEvent(const app::Event& msg,
                                   app::AppContext& ctx) {
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();
  const int x = msg.tap.x;
  const int y = msg.tap.y;
  core::Settings& s = ctx.settings;

  if (BottomBarHit(ctx.canvas(), ctx.footer_height(), 1, x, y) == 0) {
    return app::Nav::Back();
  }
  const int layout = layout_.HitTest(x, y);
  if (layout >= 0) {
    if (layout != static_cast<int>(s.tap_layout)) {
      s.tap_layout = static_cast<core::TapLayout>(layout);
      ctx.SaveSettings();
      const int top = layout_area_.y;
      ctx.RedrawRegion(
          Rect{0, top, ctx.canvas().width(), sketch_.bottom() + 2 - top},
          lgfx::epd_mode_t::epd_text);
    }
    return app::Nav::Stay();
  }
  if (sketch_.Contains(x, y)) {
    const int col = std::min(2, (x - sketch_.x) * 3 / sketch_.w);
    int row = 0;
    if (s.tap_layout == core::TapLayout::kGrid) {
      row = std::min(2, (y - sketch_.y) * 3 / sketch_.h);
      s.tap_grid[row * 3 + col] = CycleTouchAction(s.tap_grid[row * 3 + col]);
    } else {
      s.tap_columns[col] = CycleTouchAction(s.tap_columns[col]);
    }
    ctx.SaveSettings();
    ctx.RedrawRegion(zoneRect(s.tap_layout, row, col).Inset(4),
                     lgfx::epd_mode_t::epd_fast);
  } else if (reset_.Contains(x, y)) {
    s.ResetTapZones();
    ctx.SaveSettings();
    ctx.RedrawRegion(sketch_, lgfx::epd_mode_t::epd_text);
  }
  return app::Nav::Stay();
}

// ---- SwipeSettingsPage -----------------------------------------------------

namespace {

core::TouchAction* SwipeField(core::Settings& s, int i) {
  switch (i) {
    case 0:
      return &s.swipe_left;
    case 1:
      return &s.swipe_right;
    case 2:
      return &s.swipe_up;
    default:
      return &s.swipe_down;
  }
}

const char* const kSwipeLabels[4] = {"向左滑", "向右滑", "向上滑", "向下滑"};

}  // namespace

void SwipeSettingsPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  UiPainter& p = ctx.painter();
  core::Settings& s = ctx.settings;
  canvas.fillScreen(TFT_WHITE);
  const int fs = p.font_size();
  const int row_h = RowHeight(ctx);
  ctx.DrawHeader("滑动操作");
  int top = ctx.header_height() - 4;
  for (int i = 0; i < 4; ++i) {
    row_rects_[i] = Rect{0, top, canvas.width(), row_h};
    rows_[i].Draw(p, row_rects_[i], kSwipeLabels[i],
                  TouchActionLabel(*SwipeField(s, i)), /*has_arrows=*/true);
    top += row_h;
  }
  reset_ = DrawResetButton(p, top + fs);
  ctx.DrawBottomBar({"返回"});
}

app::Nav SwipeSettingsPage::handleEvent(const app::Event& msg,
                                        app::AppContext& ctx) {
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();
  const int x = msg.tap.x;
  const int y = msg.tap.y;
  core::Settings& s = ctx.settings;

  if (BottomBarHit(ctx.canvas(), ctx.footer_height(), 1, x, y) == 0) {
    return app::Nav::Back();
  }
  for (int i = 0; i < 4; ++i) {
    const int direction = rows_[i].HitTest(x, y);
    if (direction != 0) {
      core::TouchAction* field = SwipeField(s, i);
      *field = CycleTouchAction(*field, direction);
      ctx.SaveSettings();
      const Rect& r = row_rects_[i];
      ctx.RedrawRegion(Rect{r.x, r.y + 2, r.w, r.h - 4},
                       lgfx::epd_mode_t::epd_fast);
      return app::Nav::Stay();
    }
  }
  if (reset_.Contains(x, y)) {
    s.ResetSwipes();
    ctx.SaveSettings();
    const int top = row_rects_[0].y;
    ctx.RedrawRegion(
        Rect{0, top, ctx.canvas().width(), row_rects_[3].bottom() - top},
        lgfx::epd_mode_t::epd_text);
  }
  return app::Nav::Stay();
}

}  // namespace ui
}  // namespace reader
