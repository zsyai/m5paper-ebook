#include "hal/battery.h"

#include <algorithm>

#if !READER_HOST_TEST
#include <M5Unified.h>
#endif

namespace reader {
namespace hal {

int BatteryPercent(int mv) {
  // Resting voltage -> percent for the M5Paper's 4.35 V (LiHV) cell. On-device
  // charging telemetry shows the charger IC terminates at ~4252-4272 mV under
  // idle awake load, so >= 4250 mV is treated as 100%.
  static const struct {
    int mv;
    int percent;
  } kCurve[] = {
      {4250, 100}, {4200, 93}, {4100, 80}, {4000, 63}, {3900, 46},
      {3800, 30},  {3700, 15}, {3600, 6},  {3450, 0},
  };
  const int n = sizeof(kCurve) / sizeof(kCurve[0]);
  if (mv >= kCurve[0].mv) return 100;
  if (mv <= kCurve[n - 1].mv) return 0;
  for (int i = 1; i < n; ++i) {
    if (mv >= kCurve[i].mv) {
      const int hi_mv = kCurve[i - 1].mv, lo_mv = kCurve[i].mv;
      const int hi_p = kCurve[i - 1].percent, lo_p = kCurve[i].percent;
      return lo_p + (mv - lo_mv) * (hi_p - lo_p) / (hi_mv - lo_mv);
    }
  }
  return 0;
}

int ReadBatteryMv() {
#if READER_HOST_TEST
  return 4000;
#else
  int v[5];
  for (int& x : v) x = M5.Power.getBatteryVoltage();
  std::sort(v, v + 5);
  return v[2];
#endif
}

}  // namespace hal
}  // namespace reader
