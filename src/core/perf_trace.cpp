#include "core/perf_trace.h"

#if READER_PERF

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

// Output notes:
// - M5Unified leaves Serial un-started (cfg.serial_baudrate defaults to 0),
//   so Serial.print output is dropped. Perf lines go through log_printf,
//   which writes straight to UART0 via ets_printf, like the ESP log macros.
// - log_printf blocks until the UART drains (~5 ms per line at 115200), so
//   records are buffered and only printed when the calling task's outermost
//   scope ends. Printing therefore never lands inside a measured scope.

namespace perf {
namespace {

constexpr size_t kBufferSize = 8 * 1024;
constexpr size_t kLineSize = 160;

char g_buffer[kBufferSize];
size_t g_used = 0;
SemaphoreHandle_t g_lock = nullptr;

// Nesting depth is tracked per task so that the UI loop task and the state
// machine task do not corrupt each other's indentation.
thread_local int t_depth = 0;

const char* TaskName() {
  const char* name = pcTaskGetName(nullptr);
  return name != nullptr ? name : "?";
}

void Lock() {
  // First use happens in setup() before other tasks exist, so lazy creation
  // is race-free in practice.
  if (g_lock == nullptr) g_lock = xSemaphoreCreateMutex();
  xSemaphoreTake(g_lock, portMAX_DELAY);
}

void Unlock() { xSemaphoreGive(g_lock); }

// Caller must hold the lock.
void FlushLocked() {
  if (g_used == 0) return;
  g_buffer[g_used] = '\0';
  log_printf("%s", g_buffer);
  g_used = 0;
}

void Append(const char* fmt, ...) {
  char line[kLineSize];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if (len <= 0) return;
  if (static_cast<size_t>(len) >= sizeof(line)) len = sizeof(line) - 1;

  Lock();
  if (g_used + len + 1 > kBufferSize) FlushLocked();
  memcpy(g_buffer + g_used, line, len);
  g_used += len;
  // Flush once this task is back at the top level (no open scopes).
  if (t_depth == 0) FlushLocked();
  Unlock();
}

}  // namespace

int64_t NowUs() { return esp_timer_get_time(); }

Scope::Scope(const char* name)
    : name_(name), start_us_(NowUs()), depth_(t_depth++) {}

Scope::~Scope() {
  const int64_t elapsed_us = NowUs() - start_us_;
  --t_depth;
  Append("[PERF] %-13s %*s%s  %lld.%03lld ms\n", TaskName(), depth_ * 2, "",
         name_, elapsed_us / 1000, elapsed_us % 1000);
}

void Mark(const char* name) {
  const int64_t now_us = NowUs();
  Append("[PERF] %-13s @%lld.%03lld ms %s heap=%u psram=%u\n", TaskName(),
         now_us / 1000, now_us % 1000, name,
         heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void Value(const char* name, int32_t value) {
  Append("[PERF] %-13s %*s%s = %d\n", TaskName(), t_depth * 2, "", name,
         value);
}

}  // namespace perf

#endif  // READER_PERF
