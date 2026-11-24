#pragma once

// M5Paper main power latch (GPIO2) and boot timing.
//
// On battery the board is powered only while GPIO2 is driven high. Pressing
// the wheel powers the board temporarily; the firmware must latch GPIO2
// before the wheel is released or the board turns off again. These helpers
// measure how long after power-on that happens.

#include <cstdint>

namespace reader {
namespace hal {

// Microseconds since the chip was powered on or reset (RTC slow clock, so
// it includes the ROM and second-stage bootloader).
uint32_t UsSincePowerOn();

// Logs the boot timeline with `tag` (time of the earliest app code, now,
// and whether GPIO2 is driven high). Perf builds only.
void LogBootTiming(const char* tag);

}  // namespace hal
}  // namespace reader
