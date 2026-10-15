// Background TTF -> .bin glyph cache for the fonts layer.
//
// Rasterizing a TTF glyph costs about 3.5 ms (SD read + FreeType), versus
// about 1.2 ms for a pre-rendered .bin glyph, so opening a book with a TTF
// body font is about 1-2 s slower. FontCacheBuilder renders a fixed charset
// (the built-in font's GB2312 + ASCII set) into a .bin file in small time
// slices, which later opens use instead of the TTF.
//
// Output uses the tools/ttf2bin.py layout with version 2 (4 bpp, the panel's
// grey depth) and stores a key describing the source TTF in the style field.
// Bitmaps are appended in glyph index order (forward glyf reads); the index
// is sorted by unicode at the end. The file is written as <path>.tmp and
// renamed when complete, so a partial file is never opened as a cache.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "fonts/font_decoder.h"  // CharGlyphInfo

namespace reader {
namespace fonts {

class TtfGlyphSource;

// Characters that go into a TTF glyph cache: the built-in font's set (read
// from the index of its .bin image in flash, no copy) plus a few fixed
// punctuation and symbol ranges.
class Charset {
 public:
  Charset() = default;
  Charset(const uint8_t* bin_image, size_t size);

  // Upper bound of the number of characters (ranges may overlap the base).
  size_t size() const;
  bool contains(uint16_t unicode) const;
  // Calls `fn` once per character.
  void forEach(const std::function<void(uint16_t)>& fn) const;

 private:
  uint16_t baseAt(size_t i) const;
  bool baseContains(uint16_t unicode) const;

  const uint8_t* index_ = nullptr;
  size_t count_ = 0;
};

class FontCacheBuilder {
 public:
  enum class State {
    kIdle,     // nothing to do
    kPending,  // start() called, work begins on the next step()
    kWaiting,  // another builder is writing the same cache file
    kRunning,
    kDone,
    kFailed,
  };

  FontCacheBuilder() = default;
  ~FontCacheBuilder();

  FontCacheBuilder(const FontCacheBuilder&) = delete;
  FontCacheBuilder& operator=(const FontCacheBuilder&) = delete;

  // Prepares a build of `charset` from `source` into `cache_path`. No I/O
  // happens until step(). `source` and `charset` must outlive the build.
  void start(TtfGlyphSource* source, const Charset* charset,
             const std::string& cache_path, const std::string& font_name,
             const std::string& style);

  // Works for about `budget_ms`, then returns the new state.
  State step(uint32_t budget_ms);

  // Stops the build and deletes the partial file.
  void cancel();

  State state() const { return state_; }
  bool busy() const {
    return state_ == State::kPending || state_ == State::kWaiting ||
           state_ == State::kRunning;
  }

  // True while some builder is writing `cache_path`.
  static bool IsBuilding(const std::string& cache_path);

 private:
  bool begin();
  bool renderBatch();
  bool finish();
  bool flush();
  void fail(const char* what);
  void release();

  TtfGlyphSource* source_ = nullptr;
  const Charset* charset_ = nullptr;
  std::string cache_path_;
  std::string tmp_path_;
  std::string font_name_;
  std::string style_;
  State state_ = State::kIdle;
  bool claimed_ = false;

  std::vector<std::pair<uint32_t, uint16_t>> plan_;  // (glyph index, unicode)
  size_t next_ = 0;
  CharGlyphInfo* index_ = nullptr;  // PSRAM, plan_.size() entries
  size_t filled_ = 0;

  int fd_ = -1;
  uint8_t* buf_ = nullptr;  // PSRAM write buffer
  size_t buf_len_ = 0;
  uint32_t data_size_ = 0;  // bytes of bitmap data written so far

  int64_t busy_us_ = 0;
  int64_t started_us_ = 0;
};

}  // namespace fonts
}  // namespace reader
