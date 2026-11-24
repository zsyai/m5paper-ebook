#pragma once

// Battery readings for the M5Paper's 1150 mAh 4.35 V Li-Po.
//
// There is no fuel gauge: the level is estimated from the voltage, so it is
// only meaningful at rest. Right after an e-ink refresh or SD writes the
// voltage sags by 50-150 mV (more as the cell empties), which would show
// as a sudden drop that "recovers" later. Sample before such loads, or
// after a short rest.

#include <cstdint>

namespace reader {
namespace hal {

// Percent of charge for a resting voltage of a 4.35 V cell; piecewise
// linear over the cell's discharge curve (M5Unified's built-in formula is
// a straight line for 4.2 V cells and reads 100% down to about 4.1 V).
int BatteryPercent(int mv);

// Battery voltage in mV, median of a few ADC samples.
int ReadBatteryMv();

}  // namespace hal
}  // namespace reader
