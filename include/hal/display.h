#pragma once

// Panel output for the M5Paper's IT8951 e-ink display: pushing a frame and
// the policy for periodic full (GC16) refreshes that clear ghosting.

#include <M5GFX.h>

namespace reader {
namespace hal {

// Pushes `canvas` to the panel with refresh `mode`. Perf builds also wait
// for the panel refresh, so logs show the real time-to-visible.
void PushFrame(LGFX_Sprite& canvas, lgfx::epd_mode_t mode);

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
