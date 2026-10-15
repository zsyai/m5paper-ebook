// Lightweight performance tracing for boot / file-open profiling.
//
// Enabled only when compiled with -DREADER_PERF=1 (see env:m5paper_perf in
// platformio.ini). In normal builds every macro compiles to nothing, so the
// instrumentation can stay in the code permanently.
//
// Output format (Serial):
//   [PERF] <task>  <indent><name>  <elapsed> ms          (PERF_SCOPE)
//   [PERF] <task>  @<uptime> ms <name> heap=<B> psram=<B> (PERF_MARK)
//   [PERF] <task>  <indent><name> = <value>               (PERF_VALUE)
//
// Nested scopes are indented by two spaces per level. Inner scopes print
// before outer ones because a scope reports when it ends.

#pragma once

#include <stdint.h>

#ifndef READER_PERF
#define READER_PERF 0
#endif

#if READER_PERF

namespace perf {

// Microseconds since the application started.
int64_t NowUs();

// RAII timer: logs the elapsed time of the enclosing block when destroyed.
class Scope {
 public:
  explicit Scope(const char* name);
  ~Scope();

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  const char* name_;
  int64_t start_us_;
  int depth_;
};

// Logs the absolute uptime plus free heap / PSRAM at this point.
void Mark(const char* name);

// Logs a named integer (for example cache misses or bytes read).
void Value(const char* name, int32_t value);

}  // namespace perf

#define PERF_CONCAT_INNER_(a, b) a##b
#define PERF_CONCAT_(a, b) PERF_CONCAT_INNER_(a, b)
#define PERF_SCOPE(name) \
  ::perf::Scope PERF_CONCAT_(perf_scope_, __LINE__)(name)
#define PERF_MARK(name) ::perf::Mark(name)
#define PERF_VALUE(name, value) \
  ::perf::Value(name, static_cast<int32_t>(value))

#else  // READER_PERF

#define PERF_SCOPE(name) \
  do {                   \
  } while (0)
#define PERF_MARK(name) \
  do {                  \
  } while (0)
#define PERF_VALUE(name, value) \
  do {                          \
    (void)(value);              \
  } while (0)

#endif  // READER_PERF
