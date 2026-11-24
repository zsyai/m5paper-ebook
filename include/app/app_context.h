#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_APP_APP_CONTEXT_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_APP_APP_CONTEXT_H_

// Service bundle passed to `Page` methods in place of a global `UIManager`
// singleton. Owns no pages and pulls in no page headers.

#include <M5GFX.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <functional>
#include <string>
#include <vector>

#include "core/settings.h"
#include "services/book_session.h"
#include "services/font_service.h"
#include "services/frame_service.h"
#include "services/power_service.h"
#include "services/storage_service.h"
#include "ui/painter.h"
#include "ui/rect.h"

namespace reader {
namespace app {

struct AppContext {
  AppContext(core::Settings& settings_in, services::StorageService& storage_in,
             services::FontService& fonts_in, services::FrameService& frame_in,
             services::BookSession& book_in, services::PowerService& power_in)
      : settings(settings_in),
        storage(storage_in),
        fonts(fonts_in),
        frame(frame_in),
        book(book_in),
        power(power_in) {}

  core::Settings& settings;
  services::StorageService& storage;
  services::FontService& fonts;
  services::FrameService& frame;
  services::BookSession& book;
  services::PowerService& power;
  QueueHandle_t* event_queue = nullptr;

  std::function<void()> draw_current_page;
  std::function<void(const ui::Rect&, lgfx::epd_mode_t)> redraw_region;
  std::function<void()> sync_canvas_rotation;

  LGFX_Sprite& canvas() { return frame.canvas(); }
  const LGFX_Sprite& canvas() const { return frame.canvas(); }
  ui::UiPainter& painter() { return frame.painter(); }

  int font_size() const { return fonts.ui_font_size(); }
  int header_height() const { return fonts.header_height(); }
  int footer_height() const { return fonts.footer_height(); }

  bool SaveSettings() { return settings.Save(); }

  void DrawHeader(const std::string& title) {
    frame.DrawHeader(title, header_height(), power.BatteryLevel());
  }
  void DrawBottomBar(const std::vector<std::string>& items) {
    frame.DrawBottomBar(items, footer_height());
  }
  void DrawBattery(int right_x, int y) {
    frame.DrawBattery(right_x, y, power.BatteryLevel());
  }
  void ShowPrompt(const std::string& text) { frame.ShowPromptBox(text); }

  void DrawCurrentPage() {
    if (draw_current_page) draw_current_page();
  }
  void RedrawRegion(const ui::Rect& area, lgfx::epd_mode_t mode) {
    if (redraw_region) redraw_region(area, mode);
  }

  bool RunSleep(bool from_reader) {
    const QueueHandle_t q = (event_queue != nullptr) ? *event_queue : nullptr;
    return power.RunSleep(&settings, from_reader, q);
  }

  // Reading/UI setting mutators that update dependent services in lockstep.
  void SetUiFontPath(const std::string& path);
  void SetBodyFontPath(const std::string& path);
  void SetBodyFontSize(int size);
  void SetLineSpacing(float factor);
  void SetMargin(int margin);
  void SetParagraphIndent(bool on);
  void SetLineUnderline(bool on);
  void SetOrientation(core::Orientation orientation);
  void SetFullRefreshPages(int pages);
};

}  // namespace app
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_APP_APP_CONTEXT_H_
