#include "fonts/cached_ttf_glyph_source.h"

#include <esp32-hal-log.h>
#include <sys/stat.h>

#include <cstdio>

#include "core/perf_trace.h"
#include "fonts/font_cache_store.h"

namespace reader {
namespace fonts {
namespace {

constexpr char kVfsMount[] = "/sd";
// Bump when the cache layout or rendering changes to force rebuilds.
constexpr char kCacheKeyVersion[] = "ttfc3";

// Control characters have no visible glyph. They are treated as missing in
// both modes, so live and cached metrics agree.
bool IsControl(uint16_t unicode) {
  return unicode < 0x20 || (unicode >= 0x7F && unicode <= 0x9F);
}

}  // namespace

void CachedTtfGlyphSource::close() {
  builder_.cancel();
  cache_.close();
  ttf_.close();
  ttf_failed_ = false;
  open_ = false;
  path_.clear();
  cache_path_.clear();
  style_.clear();
  font_name_.clear();
  pixel_size_ = 0;
  charset_ = nullptr;
}

bool CachedTtfGlyphSource::openFile(const std::string& path,
                                    uint8_t pixel_size,
                                    const Charset* charset) {
  PERF_SCOPE("CachedTtfGlyphSource::openFile");
  close();
  struct stat st;
  if (stat((kVfsMount + path).c_str(), &st) != 0 || st.st_size <= 0) {
    return false;
  }
  path_ = path;
  pixel_size_ = pixel_size;
  charset_ = (charset != nullptr && charset->size() > 0) ? charset : nullptr;

  char key[64];
  snprintf(key, sizeof(key), "%s %ld %ld %u", kCacheKeyVersion,
           static_cast<long>(st.st_size), static_cast<long>(st.st_mtime),
           static_cast<unsigned>(pixel_size));
  style_ = key;
  cache_path_ = FontCachePath(path, pixel_size);

  if (charset_ != nullptr && openCache()) {
    font_name_ = cache_.fontName();
    open_ = true;
    log_i("fontcache: using %s", cache_path_.c_str());
    PERF_VALUE("fontcache.hit", 1);
    return true;
  }

  if (!ttf_.openFile(path, pixel_size)) {
    close();
    return false;
  }
  font_name_ = ttf_.fontName();
  open_ = true;
  if (charset_ != nullptr) {
    PERF_VALUE("fontcache.hit", 0);
    builder_.start(&ttf_, charset_, cache_path_, font_name_, style_);
  }
  return true;
}

bool CachedTtfGlyphSource::openCache() {
  if (!cache_.openFile(cache_path_)) return false;
  if (cache_.fontSize() != pixel_size_ || cache_.styleTag() != style_) {
    log_i("fontcache: %s is stale, rebuilding", cache_path_.c_str());
    cache_.close();
    return false;
  }
  return true;
}

bool CachedTtfGlyphSource::ensureTtf(uint16_t trigger) const {
  if (ttf_.isOpen()) return true;
  if (ttf_failed_) return false;
  PERF_SCOPE("CachedTtfGlyphSource::lazyTtf");
  log_i("fontcache: U+%04X is not cached, opening %s", trigger, path_.c_str());
  ttf_failed_ = !ttf_.openFile(path_, pixel_size_);
  return !ttf_failed_;
}

const CharGlyphInfo* CachedTtfGlyphSource::findGlyph(uint16_t unicode) const {
  if (!open_ || IsControl(unicode)) return nullptr;
  if (cache_.isOpen()) {
    if (const CharGlyphInfo* g = cache_.findGlyph(unicode)) return g;
    // The cache holds every charset character the TTF has.
    if (charset_ != nullptr && charset_->contains(unicode)) return nullptr;
  }
  if (!ensureTtf(unicode)) return nullptr;
  return ttf_.findGlyph(unicode);
}

size_t CachedTtfGlyphSource::loadGlyphs(
    const std::vector<uint16_t>& codepoints, const GlyphSink& sink) {
  if (!open_) return 0;
  std::vector<uint16_t> from_cache;
  std::vector<uint16_t> from_ttf;
  from_ttf.reserve(codepoints.size());
  if (cache_.isOpen()) from_cache.reserve(codepoints.size());
  for (uint16_t cp : codepoints) {
    if (IsControl(cp)) continue;
    if (cache_.isOpen()) {
      if (cache_.findGlyph(cp) != nullptr) {
        from_cache.push_back(cp);
        continue;
      }
      if (charset_ != nullptr && charset_->contains(cp)) continue;
    }
    from_ttf.push_back(cp);
  }

  size_t delivered = 0;
  if (!from_cache.empty()) delivered += cache_.loadGlyphs(from_cache, sink);
  if (!from_ttf.empty() && ensureTtf(from_ttf.front())) {
    delivered += ttf_.loadGlyphs(from_ttf, sink);
  }
  return delivered;
}

bool CachedTtfGlyphSource::stepCacheBuild(uint32_t budget_ms) {
  if (!builder_.busy()) return false;
  if (builder_.state() == FontCacheBuilder::State::kWaiting &&
      !FontCacheBuilder::IsBuilding(cache_path_) && openCache()) {
    // The other builder finished the same file.
    builder_.cancel();
    return true;
  }
  switch (builder_.step(budget_ms)) {
    case FontCacheBuilder::State::kWaiting:
      return false;
    case FontCacheBuilder::State::kDone:
      // Switch to the cache; the open TTF stays as the fallback.
      if (!openCache()) log_e("fontcache: new %s unreadable", cache_path_.c_str());
      return true;
    default:
      return true;
  }
}

}  // namespace fonts
}  // namespace reader
