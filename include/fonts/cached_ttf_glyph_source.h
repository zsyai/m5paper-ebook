// TTF glyph source backed by a pre-rendered .bin cache on the SD card.
//
// Opening a TTF first looks for a valid cache in /configs/fontcache/ (same
// file size, mtime and pixel size). If one exists, glyphs of the cached
// charset come from it and the TTF itself is only opened on demand, for
// characters outside that charset. Otherwise the TTF is rendered live and a
// FontCacheBuilder writes the cache in the background (stepCacheBuild()).
//
// Metrics are the same in both modes (the cache stores the TTF advances and
// glyphs the TTF lacks are absent from both), so page indexes stay valid
// when the cache appears.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fonts/bin_glyph_source.h"
#include "fonts/font_cache_builder.h"
#include "fonts/glyph_source.h"
#include "fonts/ttf_glyph_source.h"

namespace reader {
namespace fonts {

class CachedTtfGlyphSource : public IGlyphSource {
 public:
  CachedTtfGlyphSource() = default;
  ~CachedTtfGlyphSource() override = default;

  CachedTtfGlyphSource(const CachedTtfGlyphSource&) = delete;
  CachedTtfGlyphSource& operator=(const CachedTtfGlyphSource&) = delete;

  // Opens `path` at `pixel_size`. `charset` (must outlive the source)
  // selects the characters that are cached; nullptr disables the cache.
  bool openFile(const std::string& path, uint8_t pixel_size,
                const Charset* charset);
  void close();

  bool isOpen() const override { return open_; }
  uint8_t fontSize() const override { return pixel_size_; }
  const std::string& fontName() const override { return font_name_; }
  const CharGlyphInfo* findGlyph(uint16_t unicode) const override;
  size_t loadGlyphs(const std::vector<uint16_t>& codepoints,
                    const GlyphSink& sink) override;

  // True while the cache still has to be written.
  bool needsCacheBuild() const { return builder_.busy(); }
  // Advances the cache build by about `budget_ms`. Returns false if nothing
  // was done (no build, or another source is writing the same file).
  bool stepCacheBuild(uint32_t budget_ms);
  // Stops the build and deletes its partial file (e.g. before deep sleep).
  void cancelCacheBuild() { builder_.cancel(); }

  bool usingCache() const { return cache_.isOpen(); }
  // Cache file for the open TTF and size (whether or not it exists yet).
  const std::string& cachePath() const { return cache_path_; }

 private:
  // Opens the cache file if its key matches the TTF.
  bool openCache();
  // Opens the TTF on first use when the cache is active; `trigger` is the
  // first character that needed it (logged).
  bool ensureTtf(uint16_t trigger) const;

  std::string path_;
  std::string cache_path_;
  std::string style_;  // cache key stored in the .bin style field
  uint8_t pixel_size_ = 0;
  const Charset* charset_ = nullptr;
  bool open_ = false;
  std::string font_name_;

  BinGlyphSource cache_;
  mutable TtfGlyphSource ttf_;
  mutable bool ttf_failed_ = false;
  FontCacheBuilder builder_;
};

}  // namespace fonts
}  // namespace reader
