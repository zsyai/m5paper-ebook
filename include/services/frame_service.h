#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_FRAME_SERVICE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_FRAME_SERVICE_H_

// Owns the 8-bit grayscale offscreen canvas, UiPainter, canvas rotation,
// input coordinate mapping, and e-ink refresh cadence.

#include <M5GFX.h>

#include <string>
#include <vector>

#include "app/event.h"
#include "book/reading_style.h"
#include "core/settings.h"
#include "fonts/font_buffer.h"
#include "hal/display.h"
#include "ui/painter.h"
#include "ui/rect.h"

namespace reader {
namespace services {

class FrameService {
 public:
  static constexpr int kUiFullRefreshFrames = 5;

  explicit FrameService(PageFontCache& ui_cache);

  void Init(const std::string& ui_font_path, core::Orientation orientation,
            bool follows_reading_dir);

  LGFX_Sprite& canvas() { return canvas_; }
  const LGFX_Sprite& canvas() const { return canvas_; }
  ui::UiPainter& painter() { return painter_; }

  void SetUiFontPath(const std::string& path) { painter_.set_font_path(path); }
  void UpdateRotation(core::Orientation orientation, bool follows_reading_dir);

  // Body text rectangle in the reading direction regardless of which page
  // rotation `canvas_` currently uses.
  book::TextArea ReadingTextArea(const book::ReadingStyle& style) const;

  void MapInputToCanvas(app::Event* event) const;

  void PushCanvas(lgfx::epd_mode_t mode);
  void PushUiFrame();
  void PushUiFullRefresh();
  void PushRegion(const ui::Rect& area, lgfx::epd_mode_t mode);
  void PushReaderOverlay(bool quiet, int full_refresh_pages,
                         lgfx::epd_mode_t normal_mode);
  void PushReadingPage(int full_refresh_pages, bool force_full);

  void CountPageTurn() { reading_refresh_.Count(); }
  void RestartReadingRefresh() { reading_refresh_.Restart(); }
  void RestartAllRefreshes();

  void DrawHeader(const std::string& title, int header_h, int battery_pct);
  void DrawBottomBar(const std::vector<std::string>& items, int footer_h);
  void DrawBattery(int right_x, int y, int battery_pct);
  void ShowPromptBox(const std::string& text);
  void DrawBodyText(PageFontCache& body_cache, const book::ReadingStyle& style,
                    const std::string& text, int x, int y, int w, int h);

#if READER_PERF
  void DumpScreenshotPbm();
#endif

 private:
  LGFX_Sprite canvas_;
  ui::UiPainter painter_;
  hal::RefreshCounter reading_refresh_;
  hal::RefreshCounter ui_refresh_{kUiFullRefreshFrames - 1};
};

}  // namespace services
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_FRAME_SERVICE_H_
