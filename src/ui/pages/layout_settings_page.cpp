#include "ui/pages/layout_settings_page.h"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/app_context.h"
#include "core/perf_trace.h"
#include "core/settings.h"
#include "fonts/font_manager.h"
#include "ui/widgets/bars.h"

namespace reader {
namespace ui {

namespace {

const std::vector<int> kMarginPresets = {0, 8, 12, 16, 20, 24, 32, 40, 60, 80};
const std::vector<std::string> kOrientationLabels = {"↓", "←", "↑", "→"};
constexpr int kMaxFieldDigits = 2;
constexpr int kMaxSpacingChars = 4;

std::string RangeTitle(const char* name, int lo, int hi) {
  return std::string(name) + " " + std::to_string(lo) + "-" +
         std::to_string(hi);
}

std::string FormatSpacing(float spacing) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%.1f", spacing);
  return buf;
}

Rect DrawToggleItem(UiPainter& p, const Rect& row, int x,
                    const std::string& label, bool on) {
  const int fs = p.font_size();
  p.Prepare(label);
  p.Text(label, x, p.CenterY(row.y, row.h));
  const int toggle_h = static_cast<int>(fs * 1.8f);
  const Rect toggle{x + p.Width(label) + fs / 2, row.center_y() - toggle_h / 2,
                    static_cast<int>(fs * 2.2f), toggle_h};
  DrawToggle(p, toggle, on);
  return Rect{x, row.y, toggle.x + toggle.w - x, row.h};
}

}  // namespace

void LayoutSettingsPage::enter(app::AppContext& ctx) {
  editing_ = Field::kNone;
  keypad_.Close();
  font_options_ = {"builtin"};
  for (const auto& f : FontManager::scanAvailableFonts()) {
    font_options_.push_back(f);
  }
  font_index_ = 0;
  for (size_t i = 0; i < font_options_.size(); ++i) {
    if (font_options_[i] == ctx.settings.body_font_path) {
      font_index_ = static_cast<int>(i);
    }
  }
}

void LayoutSettingsPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  PERF_SCOPE("LayoutSettingsPage::draw");
  UiPainter& p = ctx.painter();
  const core::Settings& s = ctx.settings;
  canvas.fillScreen(TFT_WHITE);
  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int row_h = static_cast<int>(fs * 2.5f);
  ctx.DrawHeader("排版设置");

  int top = ctx.header_height() - 4;
  auto next_row = [&]() {
    const Rect row{0, top, screen_w, row_h};
    top += row_h;
    return row;
  };

  font_row_.Draw(p, next_row(), "正文字体",
                 FontManager::displayName(s.body_font_path),
                 font_options_.size() > 1);

  const Rect size_row = next_row();
  DrawRowLabel(p, size_row, "正文字号");
  if (ctx.fonts.IsBodyFontScalable(s)) {
    size_stepper_.Draw(p, size_row, std::to_string(s.body_font_size));
  } else {
    size_stepper_.Draw(p, size_row, "固定", /*enabled=*/false);
  }

  p.Prepare("阅读方向");
  const int seg_x = kRowPaddingX + p.Width("阅读方向") + fs * 2 / 3;
  const int seg_h = static_cast<int>(fs * 1.8f);
  auto segment_area = [&](const Rect& row) {
    return Rect{seg_x, row.center_y() - seg_h / 2,
                screen_w - kRowPaddingX - seg_x, seg_h};
  };

  const Rect spacing_row = next_row();
  DrawRowLabel(p, spacing_row, "行间距");
  spacing_stepper_.Draw(p, spacing_row, FormatSpacing(s.line_spacing));

  const Rect margin_row = next_row();
  DrawRowLabel(p, margin_row, "页边距");
  margin_stepper_.Draw(p, margin_row, std::to_string(s.margin));

  const Rect toggle_row = next_row();
  DrawRowLabel(p, toggle_row, "");
  indent_hit_ = DrawToggleItem(p, toggle_row, kRowPaddingX, "段首缩进",
                               s.paragraph_indent);
  underline_hit_ =
      DrawToggleItem(p, toggle_row, screen_w / 2 + kRowPaddingX / 2, "行下虚线",
                     s.line_underline);

  const Rect orientation_row = next_row();
  DrawRowLabel(p, orientation_row, "阅读方向");
  orientation_.Draw(p, segment_area(orientation_row), kOrientationLabels,
                    static_cast<int>(s.orientation));

  const int footer_top = canvas.height() - ctx.footer_height();
  const int gap = fs / 2;
  const Rect preview{kRowPaddingX, top + gap, screen_w - 2 * kRowPaddingX,
                     footer_top - gap - (top + gap)};
  canvas.drawRect(preview.x, preview.y, preview.w, preview.h, TFT_BLACK);
  ctx.book.DrawBodyPreview(preview.Inset(std::max(s.margin, 2)));

  ctx.DrawBottomBar({"返回", "操作设置"});
  keypad_.Draw(p);
}

void LayoutSettingsPage::show(app::AppContext& ctx) {
  draw(ctx, ctx.canvas());
  if (keypad_.is_open()) {
    ctx.frame.PushCanvas(lgfx::epd_mode_t::epd_fast);
  } else {
    ctx.frame.PushUiFrame();
  }
}

void LayoutSettingsPage::commit(app::AppContext& ctx) {
  ctx.SaveSettings();
  ctx.DrawCurrentPage();
}

void LayoutSettingsPage::stepBodyFont(app::AppContext& ctx, int delta) {
  const int count = static_cast<int>(font_options_.size());
  if (count <= 1) return;
  ctx.ShowPrompt("加载中...");
  font_index_ = (font_index_ + delta + count) % count;
  ctx.SetBodyFontPath(font_options_[font_index_]);
  commit(ctx);
}

void LayoutSettingsPage::stepField(app::AppContext& ctx, Field field,
                                   int direction) {
  switch (field) {
    case Field::kBodyFontSize:
      ctx.SetBodyFontSize(StepLinear(
          ctx.settings.body_font_size, core::kBodyFontSizeStep,
          core::kMinBodyFontSize, core::kMaxBodyFontSize, direction));
      break;
    case Field::kLineSpacing: {
      const int tenths = StepLinear(
          static_cast<int>(std::lround(ctx.settings.line_spacing * 10.0f)), 1,
          static_cast<int>(std::lround(core::kMinLineSpacing * 10.0f)),
          static_cast<int>(std::lround(core::kMaxLineSpacing * 10.0f)),
          direction);
      ctx.SetLineSpacing(tenths / 10.0f);
      break;
    }
    case Field::kMargin:
      ctx.SetMargin(StepPreset(ctx.settings.margin, kMarginPresets, direction));
      break;
    case Field::kNone:
      return;
  }
  commit(ctx);
}

void LayoutSettingsPage::openKeypad(app::AppContext& ctx, Field field) {
  editing_ = field;
  switch (field) {
    case Field::kBodyFontSize:
      keypad_.Open(RangeTitle("正文字号", core::kMinBodyFontSize,
                              core::kMaxBodyFontSize),
                   kMaxFieldDigits);
      break;
    case Field::kLineSpacing:
      keypad_.OpenDecimal("行间距 1.0-3.0", kMaxSpacingChars);
      break;
    case Field::kMargin:
      keypad_.Open(RangeTitle("页边距", core::kMinMargin, core::kMaxMargin),
                   kMaxFieldDigits);
      break;
    case Field::kNone:
      return;
  }
  ctx.DrawCurrentPage();
}

void LayoutSettingsPage::applyKeypad(app::AppContext& ctx) {
  if (keypad_.text().empty() || keypad_.text() == ".") return;
  if (editing_ == Field::kLineSpacing) {
    ctx.SetLineSpacing(std::strtof(keypad_.text().c_str(), nullptr));
    return;
  }
  const int value = std::stoi(keypad_.text());
  switch (editing_) {
    case Field::kBodyFontSize:
      ctx.SetBodyFontSize(value);
      break;
    case Field::kMargin:
      ctx.SetMargin(value);
      break;
    case Field::kLineSpacing:
    case Field::kNone:
      break;
  }
}

app::Nav LayoutSettingsPage::handleEvent(const app::Event& msg,
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
      return app::Nav::Replace(State::CONTROLS_SETTINGS);
    default:
      break;
  }
  const int orientation = orientation_.HitTest(x, y);
  if (orientation >= 0) {
    const auto value = static_cast<core::Orientation>(orientation);
    if (value != ctx.settings.orientation) {
      ctx.SetOrientation(value);
      commit(ctx);
    }
    return app::Nav::Stay();
  }
  if (indent_hit_.Contains(x, y)) {
    ctx.SetParagraphIndent(!ctx.settings.paragraph_indent);
    commit(ctx);
    return app::Nav::Stay();
  }
  if (underline_hit_.Contains(x, y)) {
    ctx.SetLineUnderline(!ctx.settings.line_underline);
    commit(ctx);
    return app::Nav::Stay();
  }
  if (const int delta = font_row_.HitTest(x, y)) {
    stepBodyFont(ctx, delta);
    return app::Nav::Stay();
  }
  const struct {
    const ValueStepper& stepper;
    Field field;
  } steppers[] = {
      {size_stepper_, Field::kBodyFontSize},
      {spacing_stepper_, Field::kLineSpacing},
      {margin_stepper_, Field::kMargin},
  };
  for (const auto& s : steppers) {
    switch (s.stepper.HitTest(x, y)) {
      case ValueStepper::Hit::kDecrement:
        stepField(ctx, s.field, -1);
        return app::Nav::Stay();
      case ValueStepper::Hit::kIncrement:
        stepField(ctx, s.field, +1);
        return app::Nav::Stay();
      case ValueStepper::Hit::kValue:
        openKeypad(ctx, s.field);
        return app::Nav::Stay();
      case ValueStepper::Hit::kNone:
        break;
    }
  }
  return app::Nav::Stay();
}

}  // namespace ui
}  // namespace reader
