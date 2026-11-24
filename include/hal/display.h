#pragma once

// Panel output for the M5Paper's IT8951 e-ink display: pushing a frame and
// the policy for periodic full (GC16) refreshes that clear ghosting.

#include <M5GFX.h>

namespace reader {
namespace hal {

// Pushes `canvas` to the panel with refresh `mode`. Perf builds also wait
// for the panel refresh, so logs show the real time-to-visible.
void PushFrame(LGFX_Sprite& canvas, lgfx::epd_mode_t mode);

// Pushes a full-screen 8-bit grayscale reading `canvas` directly to the IT8951
// as bulk 4bpp SPI DMA transfers without M5GFX's per-row RGB888 expansion or
// Bayer dithering, triggering GC16 when `full_refresh` is true or GL16
// otherwise. Falls back to `PushFrame` if `canvas` or panel differs.
void PushReadingFrame(LGFX_Sprite& canvas, bool full_refresh);

// Pushes only the canvas rectangle (x, y, w, h), given in the canvas's own
// (possibly rotated) coordinates, so the panel refreshes just that area.
void PushRegion(LGFX_Sprite& canvas, int x, int y, int w, int h,
                lgfx::epd_mode_t mode);

// Counts frames so that every `interval`-th one is a full refresh.
class RefreshCounter {
 public:
  // `frames` already counted; e.g. interval - 1 makes the next counted
  // frame a full refresh.
  explicit RefreshCounter(int frames = 0) : frames_(frames) {}

  void Count() { ++frames_; }
  // True if the frames counted so far are a multiple of `interval` (so also
  // right after Restart()).
  bool Due(int interval) const {
    return interval <= 1 || frames_ % interval == 0;
  }
  void Restart() { frames_ = 0; }
  // Count() then Due(); a due frame restarts the count.
  bool CountFrame(int interval);

 private:
  int frames_;
};

}  // namespace hal
}  // namespace reader
