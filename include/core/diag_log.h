#pragma once

// Append-only diagnostic logs on the SD card (/configs/<name>.txt), for
// measurements that run on battery without a serial console.
//
// Every line starts with "boot=<n> up=<minutes>m". <n> counts power-ons
// (stored in NVS), so lines from different sessions are easy to tell apart
// even though the RTC is unset; the first line a boot writes to a log is a
// "=== boot <n> fw <build time> ===" header. Each file is capped at 32 KB
// (it restarts when larger).

#include <cstdint>

namespace reader {
namespace core {

// Call once at boot, before the first DiagLog().
void InitDiagLog();

uint32_t BootNumber();

// printf-style; `name` is the file name without directory, e.g.
// "sleep_log.txt". `minutes` is the uptime to stamp (light sleep samples
// are written later than they were taken).
void DiagLog(const char* name, uint32_t minutes, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

// Uptime in minutes; esp_timer keeps counting through light sleep.
uint32_t UptimeMinutes();

}  // namespace core
}  // namespace reader
