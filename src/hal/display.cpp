#include "hal/display.h"

#include <M5Unified.h>

#include "core/perf_trace.h"

namespace reader {
namespace hal {

void PushFrame(LGFX_Sprite& canvas, lgfx::epd_mode_t mode) {
  M5.Display.setEpdMode(mode);
  {
    PERF_SCOPE("pushSprite");
    canvas.pushSprite(&M5.Display, 0, 0);
  }
#if READER_PERF
  {
    PERF_SCOPE("epd.waitDisplay");
    M5.Display.waitDisplay();
  }
#endif
}

bool RefreshCounter::CountFrame(int interval) {
  Count();
  if (!Due(interval)) return false;
  Restart();
  return true;
}

}  // namespace hal
}  // namespace reader
