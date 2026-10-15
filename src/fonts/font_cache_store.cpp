#include "fonts/font_cache_store.h"

#include <dirent.h>
#include <esp32-hal-log.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <functional>

namespace reader {
namespace fonts {
namespace {

constexpr char kVfsMount[] = "/sd";
// No trailing '/': this FatFS build (FF_FS_RPATH = 0) rejects directory
// paths that end in a separator, so opendir() would fail.
constexpr char kCacheDir[] = "/configs/fontcache";
constexpr char kTmpSuffix[] = ".tmp";

std::string BaseNameWithoutExt(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name;
}

bool IsKept(const std::string& path, const std::vector<std::string>& in_use) {
  for (const std::string& keep : in_use) {
    if (keep.empty()) continue;
    if (path == keep || path == keep + kTmpSuffix) return true;
  }
  return false;
}

// Calls `fn(sd_path, size)` for every regular file in the cache directory.
void ForEachCacheFile(
    const std::function<void(const std::string&, uint64_t)>& fn) {
  const std::string dir_path = std::string(kVfsMount) + kCacheDir;
  DIR* dir = opendir(dir_path.c_str());
  if (dir == nullptr) {
    // Normal before the first cache is written.
    log_i("fontcache: cannot open %s (errno %d)", dir_path.c_str(), errno);
    return;
  }
  // Collect first: deleting while iterating confuses FatFS readdir.
  std::vector<std::pair<std::string, uint64_t>> files;
  while (struct dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    struct stat st;
    if (stat((dir_path + "/" + name).c_str(), &st) != 0 ||
        !S_ISREG(st.st_mode)) {
      continue;
    }
    files.emplace_back(std::string(kCacheDir) + "/" + name,
                       static_cast<uint64_t>(st.st_size));
  }
  closedir(dir);
  for (const auto& file : files) fn(file.first, file.second);
}

}  // namespace

std::string FontCachePath(const std::string& ttf_path, int pixel_size) {
  return std::string(kCacheDir) + "/" + BaseNameWithoutExt(ttf_path) + "_" +
         std::to_string(pixel_size) + ".bin";
}

FontCacheUsage ScanFontCaches(const std::vector<std::string>& in_use) {
  FontCacheUsage usage;
  ForEachCacheFile([&](const std::string& path, uint64_t size) {
    usage.total_bytes += size;
    if (!IsKept(path, in_use)) {
      ++usage.removable_files;
      usage.removable_bytes += size;
    }
  });
  log_i("fontcache: %u KB in cache dir, %d removable files (%u KB)",
        static_cast<unsigned>(usage.total_bytes / 1024), usage.removable_files,
        static_cast<unsigned>(usage.removable_bytes / 1024));
  return usage;
}

int RemoveUnusedFontCaches(const std::vector<std::string>& in_use) {
  int removed = 0;
  ForEachCacheFile([&](const std::string& path, uint64_t /*size*/) {
    if (IsKept(path, in_use)) return;
    if (unlink((kVfsMount + path).c_str()) == 0) {
      log_i("fontcache: removed %s", path.c_str());
      ++removed;
    } else {
      log_e("fontcache: cannot remove %s", path.c_str());
    }
  });
  return removed;
}

std::string FormatBytes(uint64_t bytes) {
  constexpr uint64_t kKb = 1024;
  constexpr uint64_t kMb = 1024 * 1024;
  char buf[24];
  if (bytes >= kMb) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / kMb);
  } else {
    snprintf(buf, sizeof(buf), "%u KB",
             static_cast<unsigned>((bytes + kKb - 1) / kKb));
  }
  return buf;
}

}  // namespace fonts
}  // namespace reader
