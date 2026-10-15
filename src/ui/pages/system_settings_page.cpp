#include "ui/pages/system_settings_page.h"

#include <M5Unified.h>

#include "book/reading_records.h"
#include "core/perf_trace.h"
#include "font_manager.h"
#include "ui/text_renderer.h"
#include "ui/widgets/controls.h"
#include "ui_manager.h"

extern UIManager ui;

namespace reader {
namespace ui {

namespace {

int IndexOf(const std::vector<std::string>& options, const std::string& value) {
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i] == value) return static_cast<int>(i);
  }
  return 0;
}

// Button at the right end of `row`, vertically centered.
Rect RightButton(const Rect& row, int fs) {
  const int w = fs * 3;
  const int h = fs * 1.8;
  return Rect{row.right() - kRowPaddingX - w, row.center_y() - h / 2, w, h};
}

// Taps anywhere in the row's height above or below a button count.
Rect RowBand(const Rect& button, const Rect& row) {
  return Rect{button.x, row.y, button.w, row.h};
}

}  // namespace

void SystemSettingsPage::enter() {
  ui_font_options_ = {"builtin"};
  for (const auto& f : FontManager::scanAvailableFonts()) {
    if (PageFontCache::IsTtfPath(f)) ui_font_options_.push_back(f);
  }
  ui_font_index_ = IndexOf(ui_font_options_, ::ui.getUiFontPath());
  cache_usage_ = fonts::ScanFontCaches(::ui.fontCachesInUse());
  dialog_.Close();
}

void SystemSettingsPage::draw(LGFX_Sprite& canvas) {
  PERF_SCOPE("SystemSettingsPage::draw");
  UiPainter& p = ::ui.painter();
  canvas.fillScreen(TFT_WHITE);
  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int header_h = ::ui.getHeaderHeight();
  const int row_h = fs * 3;
  ::ui.drawHeader("系统设置");

  // 阅读设置 ▶
  int top = header_h - 4;
  reading_row_ = Rect{0, top, screen_w, row_h};
  DrawRowLabel(p, reading_row_, "阅读设置");
  DrawArrow(canvas, screen_w - kRowPaddingX - fs / 2, reading_row_.center_y(),
            fs * 0.4, /*right=*/true);
  top += row_h;

  // 界面字体
  const Rect font_row{0, top, screen_w, row_h};
  ui_font_row_.Draw(p, font_row, "界面字体",
                    FontManager::displayName(::ui.getUiFontPath()),
                    ui_font_options_.size() > 1);
  top += row_h;

  // 锁屏壁纸: on/off, then the file and 选择.
  const Rect lock_row{0, top, screen_w, row_h};
  const std::string lock_label = "锁屏壁纸";
  p.Prepare(lock_label);
  p.Text(lock_label, kRowPaddingX, p.CenterY(lock_row.y, lock_row.h));
  const int toggle_h = fs * 1.8;
  lock_toggle_ = Rect{kRowPaddingX + p.Width(lock_label) + fs / 2,
                      lock_row.center_y() - toggle_h / 2,
                      static_cast<int>(fs * 2.2), toggle_h};
  DrawToggle(p, lock_toggle_, ::ui.isLockScreenEnabled());

  const Rect file_row{0, lock_row.y + static_cast<int>(row_h * 0.9), screen_w,
                      static_cast<int>(row_h * 0.9)};
  const Rect select = RightButton(file_row, fs);
  DrawButton(p, select, "选择");
  wallpaper_button_ = RowBand(select, file_row);
  std::string file = ::ui.getLockScreenPath();
  const size_t slash = file.find_last_of('/');
  if (slash != std::string::npos) file = file.substr(slash + 1);
  if (file.empty()) file = "未选择";
  p.Prepare(file + "...");
  const int file_x = kRowPaddingX + fs;
  p.Text(Ellipsize(p.cache(), file, select.x - fs / 2 - file_x), file_x,
         p.CenterY(file_row.y, file_row.h));
  canvas.drawFastHLine(kRowPaddingX, file_row.bottom(),
                       screen_w - 2 * kRowPaddingX, TFT_BLACK);
  top = file_row.bottom();

  // 过期阅读记录
  const Rect clean_row{0, top, screen_w, row_h};
  DrawRowLabel(p, clean_row, "过期阅读记录");
  const Rect clean = RightButton(clean_row, fs);
  DrawButton(p, clean, "清理");
  clean_button_ = RowBand(clean, clean_row);
  top += row_h;

  // 字体缓存: total size, then 清理.
  const Rect cache_row{0, top, screen_w, row_h};
  DrawRowLabel(p, cache_row, "字体缓存");
  const Rect cache_clean = RightButton(cache_row, fs);
  DrawButton(p, cache_clean, "清理");
  cache_button_ = RowBand(cache_clean, cache_row);
  const std::string size = fonts::FormatBytes(cache_usage_.total_bytes);
  p.Prepare(size);
  p.Text(size, cache_clean.x - fs / 2 - p.Width(size),
         p.CenterY(cache_row.y, cache_row.h));
  top += row_h;

  // 使用说明
  const Rect guide_row{0, top, screen_w, row_h};
  DrawRowLabel(p, guide_row, "使用说明");
  const Rect guide = RightButton(guide_row, fs);
  DrawButton(p, guide, "阅读");
  guide_button_ = RowBand(guide, guide_row);

  ::ui.drawBottomBar({"传书", "书架", "休眠"});
  dialog_.Draw(p);
}

void SystemSettingsPage::stepUiFont(int delta) {
  const int count = static_cast<int>(ui_font_options_.size());
  if (count <= 1) return;
  ::ui.showPromptBox("加载中...");
  ui_font_index_ = (ui_font_index_ + delta + count) % count;
  ::ui.setUiFontPath(ui_font_options_[ui_font_index_]);
  ::ui.saveGlobalConfig();
  ::ui.drawCurrentPage();
}

State SystemSettingsPage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type != MessageType::TOUCH) return State::SETTING;
  const int x = msg.data.touch.x;
  const int y = msg.data.touch.y;

  if (dialog_.is_open()) {
    switch (dialog_.Press(x, y)) {
      case Dialog::Result::kConfirm:
        cleanFontCaches();
        break;
      case Dialog::Result::kCancel:
        dialog_.Close();
        ::ui.drawCurrentPage();
        break;
      case Dialog::Result::kNone:
        break;
    }
    return State::SETTING;
  }

  if (y >= M5.Display.height() - ::ui.getFooterHeight()) {
    const int col = x / (M5.Display.width() / 3);
    if (col == 0) return State::TRANSFER;
    if (col == 1) return State::BOOKSHELF;
    ::ui.enterDeepSleep(/*from_reader=*/false);
    return State::SETTING;
  }
  if (reading_row_.Contains(x, y)) return State::READING_SETTINGS;
  if (const int delta = ui_font_row_.HitTest(x, y)) {
    stepUiFont(delta);
  } else if (lock_toggle_.Contains(x, y)) {
    ::ui.setLockScreenEnabled(!::ui.isLockScreenEnabled());
    ::ui.saveGlobalConfig();
    ::ui.drawCurrentPage();
  } else if (wallpaper_button_.Contains(x, y)) {
    return State::IMAGE_PICKER;
  } else if (clean_button_.Contains(x, y)) {
    ::ui.showPromptBox("清理中...");
    book::RemoveExpiredReadingRecords();
    ::ui.drawCurrentPage();
  } else if (cache_button_.Contains(x, y)) {
    askCleanFontCaches();
  } else if (guide_button_.Contains(x, y)) {
    return State::USER_GUIDE;
  }
  return State::SETTING;
}

void SystemSettingsPage::askCleanFontCaches() {
  cache_usage_ = fonts::ScanFontCaches(::ui.fontCachesInUse());
  if (cache_usage_.removable_files == 0) {
    dialog_.Open("清理字体缓存", {"没有未使用的缓存。", "正在使用的缓存会保留。"},
                 "确定", /*cancel_label=*/"");
  } else {
    dialog_.Open("清理字体缓存",
                 {"删除 " + std::to_string(cache_usage_.removable_files) +
                      " 个未使用的缓存，",
                  "共 " + fonts::FormatBytes(cache_usage_.removable_bytes) +
                      "。",
                  "正在使用的缓存会保留。"},
                 "删除");
  }
  ::ui.drawCurrentPage();
}

void SystemSettingsPage::cleanFontCaches() {
  const bool had_files = cache_usage_.removable_files > 0;
  dialog_.Close();
  if (had_files) {
    ::ui.showPromptBox("清理中...");
    // Caches being written belong to open fonts, which are in use, so no
    // background build has to be stopped.
    const std::vector<std::string> in_use = ::ui.fontCachesInUse();
    fonts::RemoveUnusedFontCaches(in_use);
    cache_usage_ = fonts::ScanFontCaches(in_use);
  }
  ::ui.drawCurrentPage();
}

}  // namespace ui
}  // namespace reader
