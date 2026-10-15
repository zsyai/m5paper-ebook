// SD card I/O micro-benchmark (perf builds only).
//
// Measures random-read latency and sequential throughput for the different
// file I/O paths available on the Arduino core, so the font / book loaders
// can pick parameters from data instead of guesses.

#pragma once

#include <string>

namespace reader {
namespace core {

// Runs the benchmark against `sd_path` (e.g. "/fonts/x.bin") and logs
// [PERF] bench.* values. No-op unless READER_PERF is enabled.
void RunSdBenchmark(const std::string& sd_path);

}  // namespace core
}  // namespace reader
