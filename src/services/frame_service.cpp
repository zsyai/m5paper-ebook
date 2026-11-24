#include "services/frame_service.h"

#include <M5Unified.h>
#if READER_PERF
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#if READER_PERF
#include "core/debug_cmd.h"
#endif
#include "core/perf_trace.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/storage.h"
#if READER_PERF
#include "ui/canvas_pixels.h"
#endif
#include "ui/text_renderer.h"
#include "ui/widgets/bars.h"

namespace reader {
namespace services {

FrameService::FrameService(PageFontCache& ui_cache)
    : painter_(canvas_, ui_cache) {}

void FrameService::Init(const std::string& ui_font_path,
                        core::Orientation orientation,
                        bool follows_reading_dir) {
  painter_.set_font_path(ui_font_path);
  canvas_.setPsram(true);
  canvas_.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  {
    PERF_SCOPE("canvas.createSprite");
    canvas_.createSprite(M5.Display.width(), M5.Display.height());
  }
  UpdateRotation(orientation, follows_reading_dir);
  M5.Display.setEpdMode(lgfx::epd_mode_t::epd_text);
}

void FrameService::UpdateRotation(core::Orientation orientation,
                                  bool follows_reading_dir) {
  canvas_.setRotation(follows_reading_dir ? static_cast<uint8_t>(orientation)
                                          : hal::SystemRotation(orientation));
}

book::TextArea FrameService::ReadingTextArea(
    const book::ReadingStyle& style) const {
  const bool canvas_landscape = canvas_.getRotation() & 1;
  const int panel_w = canvas_landscape ? canvas_.height() : canvas_.width();
  const int panel_h = canvas_landscape ? canvas_.width() : canvas_.height();
  const bool landscape = static_cast<int>(style.orientation) & 1;
  return style.Area(landscape ? panel_h : panel_w,
                    landscape ? panel_w : panel_h);
}

void FrameService::MapInputToCanvas(app::Event* event) const {
  using app::EventType;
  if (event->type == EventType::kTap || event->type == EventType::kLongPress) {
    int x = event->tap.x;
    int y = event->tap.y;
    hal::PanelToCanvas(canvas_.getRotation(), M5.Display.width(),
                       M5.Display.height(), &x, &y);
    event->tap.x = x;
    event->tap.y = y;
  } else if (event->type == EventType::kSwipe) {
    int dx = event->swipe.dx;
    int dy = event->swipe.dy;
    hal::PanelDeltaToCanvas(canvas_.getRotation(), &dx, &dy);
    event->swipe.dx = dx;
    event->swipe.dy = dy;
  } else if (event->type == EventType::kWheel) {
    event->wheel.delta =
        hal::WheelToPageStep(event->wheel.delta, canvas_.getRotation());
  }
}

void FrameService::PushCanvas(lgfx::epd_mode_t mode) {
  hal::PushFrame(canvas_, mode);
}

void FrameService::PushUiFrame() {
  if (ui_refresh_.CountFrame(kUiFullRefreshFrames)) {
    Serial.println("UI full refresh triggered.");
    PushCanvas(lgfx::epd_mode_t::epd_quality);
  } else {
    PushCanvas(lgfx::epd_mode_t::epd_text);
  }
}

void FrameService::PushUiFullRefresh() {
  if (hal::SdErrorPending()) return;
  ui_refresh_.Restart();
  PushCanvas(lgfx::epd_mode_t::epd_quality);
}

void FrameService::PushRegion(const ui::Rect& area, lgfx::epd_mode_t mode) {
  hal::PushRegion(canvas_, area.x, area.y, area.w, area.h, mode);
}

void FrameService::PushReaderOverlay(bool quiet, int full_refresh_pages,
                                     lgfx::epd_mode_t normal_mode) {
  lgfx::epd_mode_t mode = lgfx::epd_mode_t::epd_text;
  if (!quiet) {
    mode = reading_refresh_.CountFrame(full_refresh_pages)
               ? lgfx::epd_mode_t::epd_quality
               : normal_mode;
  }
  PushCanvas(mode);
}

void FrameService::PushReadingPage(int full_refresh_pages, bool force_full) {
  const bool full = force_full || reading_refresh_.Due(full_refresh_pages);
  if (full) {
    Serial.println("Full refresh triggered.");
    reading_refresh_.Restart();
  }
  hal::PushReadingFrame(canvas_, full);
#if READER_PERF
  static uint32_t s_reading_draws = 0;
  if (++s_reading_draws % 100 == 0) {
    PERF_MARK("turn_mem_check");
    PERF_VALUE("sm.stack_hwm",
               static_cast<int32_t>(uxTaskGetStackHighWaterMark(nullptr) *
                                    sizeof(StackType_t)));
  }
#endif
}

void FrameService::RestartAllRefreshes() {
  reading_refresh_.Restart();
  ui_refresh_ = hal::RefreshCounter(kUiFullRefreshFrames - 1);
}

void FrameService::DrawHeader(const std::string& title, int header_h,
                              int battery_pct) {
  ui::DrawHeader(painter_, title, header_h, battery_pct);
}

void FrameService::DrawBottomBar(const std::vector<std::string>& items,
                                 int footer_h) {
  ui::DrawBottomBar(painter_, items, footer_h);
}

void FrameService::DrawBattery(int right_x, int y, int battery_pct) {
  ui::DrawBattery(painter_, right_x, y, battery_pct);
}

void FrameService::ShowPromptBox(const std::string& text) {
  PERF_SCOPE("FrameService::ShowPromptBox");
  if (hal::SdErrorPending()) return;
  ui::DrawPromptBox(painter_, text);
  PushCanvas(lgfx::epd_mode_t::epd_fast);
}

void FrameService::DrawBodyText(PageFontCache& body_cache,
                                const book::ReadingStyle& style,
                                const std::string& text, int x, int y, int w,
                                int h) {
  const int fs = body_cache.getFontSize();
  const int line_h = style.LineHeight(fs);
  ui::DrawText(canvas_, body_cache, text, x, y, line_h);
  if (style.line_underline) {
    ui::DrawLineGuides(canvas_, x, y, w, h, fs, line_h);
  }
}

#if READER_PERF
void FrameService::DumpScreenshotPbm() {
  ui::CanvasPixels px(canvas_);
  if (!px.valid()) {
    log_printf("[E2E] error=no_canvas\n");
    return;
  }
  const int w = px.width();
  const int h = px.height();
  const int row_bytes = (w + 7) / 8;
  std::vector<uint8_t> pbm(static_cast<size_t>(row_bytes) * h);
  std::vector<uint8_t> gray_row(static_cast<size_t>(w));
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      gray_row[static_cast<size_t>(x)] = *px.at(x, y);
    }
    core::PackGrayscaleRowTo1BitPbm(
        gray_row.data(), w, pbm.data() + static_cast<size_t>(y) * row_bytes);
  }
  std::vector<uint8_t> rle;
  core::RleEncodePackBits(pbm.data(), pbm.size(), &rle);
  const uint32_t crc = core::Crc32(pbm.data(), pbm.size());
  const std::string b64 = core::Base64Encode(rle.data(), rle.size());

  log_printf(
      "[SCREENSHOT_BEGIN w=%d h=%d pbm_bytes=%u rle_bytes=%u crc32=%08x]\n", w,
      h, static_cast<unsigned>(pbm.size()), static_cast<unsigned>(rle.size()),
      static_cast<unsigned>(crc));
  constexpr size_t kLineCols = 76;
  for (size_t pos = 0; pos < b64.size(); pos += kLineCols) {
    const size_t n = std::min(kLineCols, b64.size() - pos);
    log_printf("[SCREENSHOT_DATA] %.*s\n", static_cast<int>(n),
               b64.data() + pos);
  }
  log_printf("[SCREENSHOT_END]\n");
}
#endif  // READER_PERF

}  // namespace services
}  // namespace reader
