#pragma once

// The TTF glyph cache files in /configs/fontcache/ (see FontCacheBuilder):
// their names, total size, and removal of the ones no open font uses.
//
// One cache file exists per TTF and pixel size, so trying several fonts and
// sizes leaves several MB behind each time (a 36px cache is about 4 MB).

#include <cstdint>
#include <string>
#include <vector>

namespace reader {
namespace fonts {

// SD path of the cache for the TTF at `ttf_path` rendered at `pixel_size`.
std::string FontCachePath(const std::string& ttf_path, int pixel_size);

struct FontCacheUsage {
  uint64_t total_bytes = 0;      // Every file in the cache directory.
  int removable_files = 0;       // Files that RemoveUnusedFontCaches() deletes.
  uint64_t removable_bytes = 0;
};

// `in_use` lists cache paths (FontCachePath()) to keep; their unfinished
// ".tmp" files are kept too, so a cache that is being written survives.
// Everything else in the directory counts as removable.
FontCacheUsage ScanFontCaches(const std::vector<std::string>& in_use);

// Deletes the removable files. Returns the number deleted.
int RemoveUnusedFontCaches(const std::vector<std::string>& in_use);

// "12.4 MB", "640 KB" or "0 KB".
std::string FormatBytes(uint64_t bytes);

}  // namespace fonts
}  // namespace reader
