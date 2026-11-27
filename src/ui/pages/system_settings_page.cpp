#include "ui/pages/system_settings_page.h"

#include <M5Unified.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app/app_context.h"
#include "book/reading_records.h"
#include "core/perf_trace.h"
#include "core/version.h"
#include "fonts/font_manager.h"
#include "i18n/i18n.h"
#include "ui/text_renderer.h"
#include "ui/widgets/bars.h"
#include "ui/widgets/controls.h"

namespace reader {
namespace ui {

namespace {

int IndexOf(const std::vector<std::string>& options, const std::string& value) {
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i] == value) return static_cast<int>(i);
  }
  return 0;
}

Rect RightButton(const Rect& row, int fs, int w) {
  const int h = static_cast<int>(fs * 1.8f);
  return Rect{row.right() - kRowPaddingX - w, row.center_y() - h / 2, w, h};
}

Rect RowBand(const Rect& button, const Rect& row) {
  return Rect{button.x, row.y, button.w, row.h};
}

}  // namespace

void SystemSettingsPage::enter(app::AppContext& ctx) {
  ui_font_options_ = {"builtin"};
  for (const auto& f : FontManager::scanAvailableFonts()) {
    if (PageFontCache::IsTtfPath(f)) ui_font_options_.push_back(f);
  }
  ui_font_index_ =
      IndexOf(ui_font_options_, ctx.fonts.EffectiveUiFontPath(ctx.settings));
  expired_records_ = book::ScanExpiredReadingRecords();
  cache_usage_ = fonts::ScanFontCaches(ctx.fonts.CachesInUse(ctx.settings));
  dialog_.Close();
}

void SystemSettingsPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  PERF_SCOPE("SystemSettingsPage::draw");
  UiPainter& p = ctx.painter();
  const core::Settings& s = ctx.settings;
  canvas.fillScreen(TFT_WHITE);
  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int header_h = ctx.header_height();
  const int row_h = fs * 3;
  ctx.DrawHeader(TR("系统设置"));

  // The right-hand buttons share one width, wide enough for every label.
  const char* const button_labels[] = {TR("选择"), TR("清理"), TR("阅读")};
  int button_w = fs * 3;
  for (const char* label : button_labels) {
    p.Prepare(label);
    button_w = std::max(button_w, p.Width(label) + fs);
  }

  int top = header_h - 4;
  reading_row_ = Rect{0, top, screen_w, row_h};
  DrawRowLabel(p, reading_row_, TRC("settings", "阅读设置"));
  DrawArrow(canvas, screen_w - kRowPaddingX - fs / 2, reading_row_.center_y(),
            static_cast<int>(fs * 0.4f), /*right=*/true);
  top += row_h;

  const Rect font_row{0, top, screen_w, row_h};
  ui_font_row_.Draw(p, font_row, TR("界面字体"),
                    FontManager::displayName(ctx.fonts.EffectiveUiFontPath(s)),
                    ui_font_options_.size() > 1);
  top += row_h;

  sleep_row_.Draw(
      p, Rect{0, top, screen_w, row_h}, TR("休眠模式"),
      s.sleep_mode == core::SleepMode::kLight ? TR("浅休眠") : TR("深度休眠"),
      /*has_arrows=*/true);
  top += row_h;

  language_row_.Draw(p, Rect{0, top, screen_w, row_h}, TR("语言"),
                     i18n::LanguageName(i18n::CurrentLanguage()),
                     /*has_arrows=*/true);
  top += row_h;

  const Rect lock_row{0, top, screen_w, row_h};
  const std::string lock_label = TR("锁屏壁纸");
  p.Prepare(lock_label);
  p.Text(lock_label, kRowPaddingX, p.CenterY(lock_row.y, lock_row.h));
  const int toggle_h = static_cast<int>(fs * 1.8f);
  lock_toggle_ = Rect{kRowPaddingX + p.Width(lock_label) + fs / 2,
                      lock_row.center_y() - toggle_h / 2,
                      static_cast<int>(fs * 2.2f), toggle_h};
  DrawToggle(p, lock_toggle_, s.lock_screen_enabled);

  const Rect file_row{0, lock_row.y + static_cast<int>(row_h * 0.9f), screen_w,
                      static_cast<int>(row_h * 0.9f)};
  const Rect select = RightButton(file_row, fs, button_w);
  DrawButton(p, select, TR("选择"));
  wallpaper_button_ = RowBand(select, file_row);
  std::string file = s.lock_screen_path;
  const size_t slash = file.find_last_of('/');
  if (slash != std::string::npos) file = file.substr(slash + 1);
  if (file.empty()) file = TR("未选择");
  p.Prepare(file + "...");
  const int file_x = kRowPaddingX + fs;
  p.Text(Ellipsize(p.cache(), file, select.x - fs / 2 - file_x), file_x,
         p.CenterY(file_row.y, file_row.h));
  canvas.drawFastHLine(kRowPaddingX, file_row.bottom(),
                       screen_w - 2 * kRowPaddingX, TFT_BLACK);
  top = file_row.bottom();

  const Rect clean_row{0, top, screen_w, row_h};
  DrawRowLabel(p, clean_row, TR("过期阅读记录"));
  const Rect clean = RightButton(clean_row, fs, button_w);
  DrawButton(p, clean, TR("清理"));
  clean_button_ = RowBand(clean, clean_row);
  const std::string expired_size = fonts::FormatBytes(expired_records_.bytes);
  p.Prepare(expired_size);
  p.Text(expired_size, clean.x - fs / 2 - p.Width(expired_size),
         p.CenterY(clean_row.y, clean_row.h));
  top += row_h;

  const Rect cache_row{0, top, screen_w, row_h};
  DrawRowLabel(p, cache_row, TR("字体缓存"));
  const Rect cache_clean = RightButton(cache_row, fs, button_w);
  DrawButton(p, cache_clean, TR("清理"));
  cache_button_ = RowBand(cache_clean, cache_row);
  const std::string size = fonts::FormatBytes(cache_usage_.total_bytes);
  p.Prepare(size);
  p.Text(size, cache_clean.x - fs / 2 - p.Width(size),
         p.CenterY(cache_row.y, cache_row.h));
  top += row_h;

  const Rect guide_row{0, top, screen_w, row_h};
  DrawRowLabel(p, guide_row, TR("使用说明"));
  const Rect guide = RightButton(guide_row, fs, button_w);
  DrawButton(p, guide, TR("阅读"));
  guide_button_ = RowBand(guide, guide_row);
  const std::string version = std::string("v") + core::kFirmwareVersion;
  p.Prepare(version);
  p.Text(version, guide.x - fs / 2 - p.Width(version),
         p.CenterY(guide_row.y, guide_row.h));

  ctx.DrawBottomBar({TR("传书"), TR("书架"), TR("休眠")});
  dialog_.Draw(p);
}

void SystemSettingsPage::stepUiFont(app::AppContext& ctx, int delta) {
  const int count = static_cast<int>(ui_font_options_.size());
  if (count <= 1) return;
  ctx.ShowPrompt(TR("加载中..."));
  ui_font_index_ = (ui_font_index_ + delta + count) % count;
  ctx.SetUiFontPath(ui_font_options_[ui_font_index_]);
  ctx.SaveSettings();
  ctx.DrawCurrentPage();
}

app::Nav SystemSettingsPage::handleEvent(const app::Event& msg,
                                         app::AppContext& ctx) {
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();
  const int x = msg.tap.x;
  const int y = msg.tap.y;

  if (dialog_.is_open()) {
    switch (dialog_.Press(x, y)) {
      case Dialog::Result::kConfirm:
        cleanFontCaches(ctx);
        break;
      case Dialog::Result::kCancel:
        dialog_.Close();
        ctx.DrawCurrentPage();
        break;
      case Dialog::Result::kNone:
        break;
    }
    return app::Nav::Stay();
  }

  const int footer_col =
      BottomBarHit(ctx.canvas(), ctx.footer_height(), /*count=*/3, x, y);
  if (footer_col == 0) return app::Nav::Root(State::TRANSFER);
  if (footer_col == 1) return app::Nav::Root(State::BOOKSHELF);
  if (footer_col == 2) return app::Nav::Push(State::SLEEP);
  if (reading_row_.Contains(x, y))
    return app::Nav::Push(State::READING_SETTINGS);
  if (const int delta = ui_font_row_.HitTest(x, y)) {
    stepUiFont(ctx, delta);
  } else if (sleep_row_.HitTest(x, y) != 0) {
    ctx.settings.sleep_mode = ctx.settings.sleep_mode == core::SleepMode::kLight
                                  ? core::SleepMode::kDeep
                                  : core::SleepMode::kLight;
    ctx.SaveSettings();
    ctx.DrawCurrentPage();
  } else if (const int delta = language_row_.HitTest(x, y)) {
    const int count = i18n::kLanguageCount;
    const int next =
        (static_cast<int>(ctx.settings.language) + delta + count) % count;
    ctx.settings.language = static_cast<i18n::Language>(next);
    i18n::SetLanguage(ctx.settings.language);
    ctx.SaveSettings();
    ctx.DrawCurrentPage();
  } else if (lock_toggle_.Contains(x, y)) {
    ctx.settings.lock_screen_enabled = !ctx.settings.lock_screen_enabled;
    ctx.SaveSettings();
    ctx.DrawCurrentPage();
  } else if (wallpaper_button_.Contains(x, y)) {
    return app::Nav::Push(State::IMAGE_PICKER);
  } else if (clean_button_.Contains(x, y)) {
    ctx.ShowPrompt(TR("清理中..."));
    book::RemoveExpiredReadingRecords();
    expired_records_ = book::ScanExpiredReadingRecords();
    ctx.DrawCurrentPage();
  } else if (cache_button_.Contains(x, y)) {
    askCleanFontCaches(ctx);
  } else if (guide_button_.Contains(x, y)) {
    return app::Nav::Push(State::USER_GUIDE);
  }
  return app::Nav::Stay();
}

void SystemSettingsPage::askCleanFontCaches(app::AppContext& ctx) {
  cache_usage_ = fonts::ScanFontCaches(ctx.fonts.CachesInUse(ctx.settings));
  if (cache_usage_.removable_files == 0) {
    dialog_.Open(TR("清理字体缓存"),
                 {TR("没有未使用的缓存。"), TR("正在使用的缓存会保留。")}, TR("确定"),
                 /*cancel_label=*/"");
  } else {
    dialog_.Open(
        TR("清理字体缓存"),
        {TR("删除 ") + std::to_string(cache_usage_.removable_files) +
             TR(" 个未使用的缓存，"),
         TR("共 ") + fonts::FormatBytes(cache_usage_.removable_bytes) + TR("。"),
         TR("正在使用的缓存会保留。")},
        TR("删除"));
  }
  ctx.DrawCurrentPage();
}

void SystemSettingsPage::cleanFontCaches(app::AppContext& ctx) {
  const bool had_files = cache_usage_.removable_files > 0;
  dialog_.Close();
  if (had_files) {
    ctx.ShowPrompt(TR("清理中..."));
    const std::vector<std::string> in_use = ctx.fonts.CachesInUse(ctx.settings);
    fonts::RemoveUnusedFontCaches(in_use);
    cache_usage_ = fonts::ScanFontCaches(in_use);
  }
  ctx.DrawCurrentPage();
}

}  // namespace ui
}  // namespace reader
