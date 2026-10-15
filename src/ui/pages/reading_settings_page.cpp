#include "ui/pages/reading_settings_page.h"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>

#include "core/perf_trace.h"
#include "core/settings.h"
#include "font_manager.h"
#include "ui_manager.h"

extern UIManager ui;

namespace reader {
namespace ui {

namespace {

const std::vector<int> kMarginPresets = {0, 8, 12, 16, 20, 24, 32, 40, 60, 80};
const std::vector<std::string> kSpacingLabels = {"1.0", "1.5", "2.0", "2.5",
                                                 "3.0"};
// Arrows point the way reading advances (towards the bottom of the text),
// in core::Orientation order: kUp reads downwards, kRight (text top towards
// the wheel side) reads to the left, and so on.
const std::vector<std::string> kOrientationLabels = {"↓", "←", "↑", "→"};
constexpr int kMaxFieldDigits = 2;

// ASCII '-': the built-in UI font (GB2312 + ASCII) has no en dash.
std::string RangeTitle(const char* name, int lo, int hi) {
  return std::string(name) + " " + std::to_string(lo) + "-" +
         std::to_string(hi);
}

// Draws "label [开/关]" starting at `x` in `row` and returns its tap area
// (label and toggle, full row height).
Rect DrawToggleItem(UiPainter& p, const Rect& row, int x,
                    const std::string& label, bool on) {
  const int fs = p.font_size();
  p.Prepare(label);
  p.Text(label, x, p.CenterY(row.y, row.h));
  const int toggle_h = fs * 1.8;
  const Rect toggle{x + p.Width(label) + fs / 2, row.center_y() - toggle_h / 2,
                    static_cast<int>(fs * 2.2), toggle_h};
  DrawToggle(p, toggle, on);
  return Rect{x, row.y, toggle.x + toggle.w - x, row.h};
}

int SpacingIndex(float spacing) {
  return static_cast<int>(std::lround((spacing - core::kMinLineSpacing) /
                                      core::kLineSpacingStep));
}

}  // namespace

void ReadingSettingsPage::enter() {
  // Still the previous state while the new page is entered.
  return_state_ =
      get_current_state() == State::SETTING ? State::SETTING : State::READER;
  editing_ = Field::kNone;
  keypad_.Close();
  font_options_ = {"builtin"};
  for (const auto& f : FontManager::scanAvailableFonts()) {
    font_options_.push_back(f);
  }
  font_index_ = 0;
  for (size_t i = 0; i < font_options_.size(); ++i) {
    if (font_options_[i] == ::ui.getFontPath()) font_index_ = i;
  }
}

void ReadingSettingsPage::draw(LGFX_Sprite& canvas) {
  PERF_SCOPE("ReadingSettingsPage::draw");
  UiPainter& p = ::ui.painter();
  canvas.fillScreen(TFT_WHITE);
  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int row_h = fs * 2.5;
  ::ui.drawHeader("阅读设置");

  int top = ::ui.getHeaderHeight() - 4;
  auto next_row = [&]() {
    const Rect row{0, top, screen_w, row_h};
    top += row_h;
    return row;
  };

  font_row_.Draw(p, next_row(), "正文字体",
                 FontManager::displayName(::ui.getFontPath()),
                 font_options_.size() > 1);

  const Rect size_row = next_row();
  DrawRowLabel(p, size_row, "正文字号");
  if (::ui.isBodyFontScalable()) {
    size_stepper_.Draw(p, size_row, std::to_string(::ui.getBodyFontSize()));
  } else {
    // .bin fonts are pre-rendered at one size.
    size_stepper_.Draw(p, size_row, "固定", /*enabled=*/false);
  }

  // Both segmented rows start after the longest label (阅读方向).
  p.Prepare("阅读方向");
  const int seg_x = kRowPaddingX + p.Width("阅读方向") + fs * 2 / 3;
  const int seg_h = fs * 1.8;
  auto segment_area = [&](const Rect& row) {
    return Rect{seg_x, row.center_y() - seg_h / 2,
                screen_w - kRowPaddingX - seg_x, seg_h};
  };

  const Rect spacing_row = next_row();
  DrawRowLabel(p, spacing_row, "行间距");
  spacing_.Draw(p, segment_area(spacing_row), kSpacingLabels,
                SpacingIndex(::ui.getLineSpacingFactor()));

  const Rect margin_row = next_row();
  DrawRowLabel(p, margin_row, "页边距");
  margin_stepper_.Draw(p, margin_row, std::to_string(::ui.getMargin()));

  // Two on/off settings share a row: 段首缩进 on the left half, 行下虚线 on
  // the right half.
  const Rect toggle_row = next_row();
  DrawRowLabel(p, toggle_row, "");
  indent_hit_ = DrawToggleItem(p, toggle_row, kRowPaddingX, "段首缩进",
                               ::ui.getParagraphIndent());
  underline_hit_ = DrawToggleItem(p, toggle_row, screen_w / 2 + kRowPaddingX / 2,
                                  "行下虚线", ::ui.getLineUnderline());

  const Rect orientation_row = next_row();
  DrawRowLabel(p, orientation_row, "阅读方向");
  orientation_.Draw(p, segment_area(orientation_row), kOrientationLabels,
                    static_cast<int>(::ui.getOrientation()));

  const Rect refresh_row = next_row();
  DrawRowLabel(p, refresh_row, "全刷间隔");
  refresh_stepper_.Draw(p, refresh_row,
                        std::to_string(::ui.getFullRefreshPages()) + " 页");

  const int footer_top = canvas.height() - ::ui.getFooterHeight();
  const int gap = fs / 2;
  const Rect preview{kRowPaddingX, top + gap, screen_w - 2 * kRowPaddingX,
                     footer_top - gap - (top + gap)};
  canvas.drawRect(preview.x, preview.y, preview.w, preview.h, TFT_BLACK);
  // The box stands for the screen, so the text sits the page margin in
  // (at least 2px, clear of the border).
  ::ui.drawBodyPreview(preview.Inset(std::max(::ui.getMargin(), 2)));

  ::ui.drawBottomBar({"返回"});
  back_ = Rect{0, footer_top, screen_w, ::ui.getFooterHeight()};

  keypad_.Draw(p);
}

void ReadingSettingsPage::show() {
  draw(::ui.getCanvas());
  if (keypad_.is_open()) {
    ::ui.pushCanvas(lgfx::epd_mode_t::epd_fast);
  } else {
    ::ui.pushUiFrame();
  }
}

void ReadingSettingsPage::commit() {
  ::ui.saveGlobalConfig();
  ::ui.drawCurrentPage();
}

void ReadingSettingsPage::stepBodyFont(int delta) {
  const int count = static_cast<int>(font_options_.size());
  if (count <= 1) return;
  ::ui.showPromptBox("加载中...");
  font_index_ = (font_index_ + delta + count) % count;
  ::ui.setFontPath(font_options_[font_index_]);
  commit();
}

void ReadingSettingsPage::stepField(Field field, int direction) {
  switch (field) {
    case Field::kBodyFontSize:
      ::ui.setBodyFontSize(StepLinear(
          ::ui.getBodyFontSize(), core::kBodyFontSizeStep,
          core::kMinBodyFontSize, core::kMaxBodyFontSize, direction));
      break;
    case Field::kMargin:
      ::ui.setMargin(StepPreset(::ui.getMargin(), kMarginPresets, direction));
      break;
    case Field::kFullRefreshPages:
      ::ui.setFullRefreshPages(StepLinear(::ui.getFullRefreshPages(), 1,
                                        core::kMinFullRefreshPages,
                                        core::kMaxFullRefreshPages, direction));
      break;
    case Field::kNone:
      return;
  }
  commit();
}

void ReadingSettingsPage::openKeypad(Field field) {
  editing_ = field;
  switch (field) {
    case Field::kBodyFontSize:
      keypad_.Open(RangeTitle("正文字号", core::kMinBodyFontSize,
                              core::kMaxBodyFontSize),
                   kMaxFieldDigits);
      break;
    case Field::kMargin:
      keypad_.Open(RangeTitle("页边距", core::kMinMargin, core::kMaxMargin),
                   kMaxFieldDigits);
      break;
    case Field::kFullRefreshPages:
      keypad_.Open(RangeTitle("全刷间隔", core::kMinFullRefreshPages,
                              core::kMaxFullRefreshPages),
                   kMaxFieldDigits);
      break;
    case Field::kNone:
      return;
  }
  ::ui.drawCurrentPage();
}

void ReadingSettingsPage::applyKeypad() {
  if (keypad_.text().empty()) return;
  // The setters clamp out-of-range values (odd font sizes round down).
  const int value = std::stoi(keypad_.text());
  switch (editing_) {
    case Field::kBodyFontSize:
      ::ui.setBodyFontSize(value);
      break;
    case Field::kMargin:
      ::ui.setMargin(value);
      break;
    case Field::kFullRefreshPages:
      ::ui.setFullRefreshPages(value);
      break;
    case Field::kNone:
      break;
  }
}

State ReadingSettingsPage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type != MessageType::TOUCH) return State::READING_SETTINGS;
  const int x = msg.data.touch.x;
  const int y = msg.data.touch.y;

  if (keypad_.is_open()) {
    switch (keypad_.Press(x, y)) {
      case Keypad::Key::kEdit:
      case Keypad::Key::kMode:
        ::ui.drawCurrentPage();
        break;
      case Keypad::Key::kOk:
        applyKeypad();
        keypad_.Close();
        editing_ = Field::kNone;
        commit();
        break;
      case Keypad::Key::kCancel:
        keypad_.Close();
        editing_ = Field::kNone;
        ::ui.drawCurrentPage();
        break;
      case Keypad::Key::kNone:
        break;
    }
    return State::READING_SETTINGS;
  }

  if (back_.Contains(x, y)) return return_state_;
  const int orientation = orientation_.HitTest(x, y);
  if (orientation >= 0) {
    const auto value = static_cast<core::Orientation>(orientation);
    if (value != ::ui.getOrientation()) {
      ::ui.setOrientation(value);
      commit();
    }
    return State::READING_SETTINGS;
  }
  if (indent_hit_.Contains(x, y)) {
    ::ui.setParagraphIndent(!::ui.getParagraphIndent());
    commit();
    return State::READING_SETTINGS;
  }
  if (underline_hit_.Contains(x, y)) {
    ::ui.setLineUnderline(!::ui.getLineUnderline());
    commit();
    return State::READING_SETTINGS;
  }
  if (const int delta = font_row_.HitTest(x, y)) {
    stepBodyFont(delta);
    return State::READING_SETTINGS;
  }
  const int spacing = spacing_.HitTest(x, y);
  if (spacing >= 0) {
    const float factor =
        core::kMinLineSpacing + spacing * core::kLineSpacingStep;
    if (factor != ::ui.getLineSpacingFactor()) {
      ::ui.setLineSpacingFactor(factor);
      commit();
    }
    return State::READING_SETTINGS;
  }
  const struct {
    const ValueStepper& stepper;
    Field field;
  } steppers[] = {
      {size_stepper_, Field::kBodyFontSize},
      {margin_stepper_, Field::kMargin},
      {refresh_stepper_, Field::kFullRefreshPages},
  };
  for (const auto& s : steppers) {
    switch (s.stepper.HitTest(x, y)) {
      case ValueStepper::Hit::kDecrement:
        stepField(s.field, -1);
        return State::READING_SETTINGS;
      case ValueStepper::Hit::kIncrement:
        stepField(s.field, +1);
        return State::READING_SETTINGS;
      case ValueStepper::Hit::kValue:
        openKeypad(s.field);
        return State::READING_SETTINGS;
      case ValueStepper::Hit::kNone:
        break;
    }
  }
  return State::READING_SETTINGS;
}

}  // namespace ui
}  // namespace reader
