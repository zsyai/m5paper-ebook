#include "core/sd_bench.h"

#include "core/perf_trace.h"

#if READER_PERF

#include <SD.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

namespace reader {
namespace core {
namespace {

constexpr int kRandomReads = 24;
constexpr size_t kSmallRead = 200;          // about one glyph bitmap
constexpr size_t kSeqTotal = 128 * 1024;    // sequential sample size

std::vector<uint32_t> RandomOffsets(uint32_t file_size) {
  std::vector<uint32_t> offsets;
  uint32_t x = 12345;
  for (int i = 0; i < kRandomReads; ++i) {
    x = x * 1103515245u + 12345u;
    offsets.push_back((x >> 4) % (file_size - kSmallRead));
  }
  return offsets;
}

int32_t UsPerOp(int64_t start_us, int ops) {
  return static_cast<int32_t>((esp_timer_get_time() - start_us) / ops);
}

void BenchStdio(const std::string& path, const std::vector<uint32_t>& offs,
                size_t buffer_size, const char* label) {
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return;
  if (buffer_size != 0) f.setBufferSize(buffer_size);
  uint8_t buf[kSmallRead];
  const int64_t t0 = esp_timer_get_time();
  for (uint32_t off : offs) {
    f.seek(off);
    f.read(buf, sizeof(buf));
  }
  PERF_VALUE(label, UsPerOp(t0, offs.size()));
  f.close();
}

void BenchPosixRandom(const std::string& vfs_path,
                      const std::vector<uint32_t>& offs, const char* label) {
  const int fd = ::open(vfs_path.c_str(), O_RDONLY);
  if (fd < 0) return;
  uint8_t buf[kSmallRead];
  const int64_t t0 = esp_timer_get_time();
  for (uint32_t off : offs) {
    ::lseek(fd, off, SEEK_SET);
    ::read(fd, buf, sizeof(buf));
  }
  PERF_VALUE(label, UsPerOp(t0, offs.size()));
  ::close(fd);
}

void BenchPosixSequential(const std::string& vfs_path, uint32_t start,
                          size_t chunk, const char* label) {
  const int fd = ::open(vfs_path.c_str(), O_RDONLY);
  if (fd < 0) return;
  auto* buf = static_cast<uint8_t*>(heap_caps_malloc(chunk, MALLOC_CAP_SPIRAM));
  if (buf != nullptr) {
    ::lseek(fd, start, SEEK_SET);
    const int64_t t0 = esp_timer_get_time();
    size_t total = 0;
    while (total < kSeqTotal) {
      const ssize_t n = ::read(fd, buf, chunk);
      if (n <= 0) break;
      total += n;
    }
    const int64_t us = esp_timer_get_time() - t0;
    // KB per second.
    PERF_VALUE(label, us > 0 ? static_cast<int32_t>(total * 1000000LL / 1024 / us) : 0);
    free(buf);
  }
  ::close(fd);
}

}  // namespace

void RunSdBenchmark(const std::string& sd_path) {
  PERF_SCOPE("SdBenchmark");
  const std::string vfs_path = "/sd" + sd_path;
  struct stat st;
  if (::stat(vfs_path.c_str(), &st) != 0 || st.st_size < 256 * 1024) {
    PERF_VALUE("bench.skipped", 1);
    return;
  }
  PERF_VALUE("bench.file_kb", st.st_size / 1024);
  PERF_VALUE("bench.st_blksize", st.st_blksize);

  std::vector<uint32_t> offs = RandomOffsets(st.st_size);
  std::vector<uint32_t> sorted = offs;
  std::sort(sorted.begin(), sorted.end());

  // Random-order small reads (us per read).
  BenchStdio(sd_path, offs, 0, "bench.stdio4k_random_us");
  BenchStdio(sd_path, offs, 512, "bench.stdio512_random_us");
  BenchPosixRandom(vfs_path, offs, "bench.posix_random_us");
  // Same offsets in ascending order (forward-only seeks).
  BenchStdio(sd_path, sorted, 512, "bench.stdio512_sorted_us");
  BenchPosixRandom(vfs_path, sorted, "bench.posix_sorted_us");
  // Short forward hops near the start vs near the end of the file.
  std::vector<uint32_t> near_start, near_end;
  for (int i = 0; i < kRandomReads; ++i) {
    near_start.push_back(64 * 1024 + i * 4096);
    near_end.push_back(st.st_size - 256 * 1024 + i * 4096);
  }
  BenchPosixRandom(vfs_path, near_start, "bench.posix_hop4k_start_us");
  BenchPosixRandom(vfs_path, near_end, "bench.posix_hop4k_end_us");
  // Sequential throughput (KB/s) for different chunk sizes.
  BenchPosixSequential(vfs_path, st.st_size / 2, 4096, "bench.posix_seq4k_kbps");
  BenchPosixSequential(vfs_path, st.st_size / 2, 32768, "bench.posix_seq32k_kbps");
}

}  // namespace core
}  // namespace reader

#else  // READER_PERF

namespace reader {
namespace core {
void RunSdBenchmark(const std::string&) {}
}  // namespace core
}  // namespace reader

#endif  // READER_PERF
