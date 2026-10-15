#include "fonts/font_cache_builder.h"

#include <esp32-hal-log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "core/perf_trace.h"
#include "fonts/ttf_glyph_source.h"

namespace reader {
namespace fonts {
namespace {

// .bin layout (see tools/ttf2bin.py).
constexpr size_t kHeaderSize = 134;
constexpr size_t kFieldSize = 64;
constexpr size_t kNameOffset = 6;
constexpr size_t kStyleOffset = kNameOffset + kFieldSize;
constexpr size_t kEntrySize = sizeof(CharGlyphInfo);
constexpr uint8_t kVersion4Bpp = 2;

constexpr char kVfsMount[] = "/sd";
constexpr size_t kWriteBufferSize = 16 * 1024;
// Glyphs per loadGlyphs() call; ~3.5 ms each, so the time budget is checked
// every ~15 ms.
constexpr size_t kBatchSize = 4;

std::vector<std::string>& ActivePaths() {
  static std::vector<std::string> paths;
  return paths;
}

std::string SdPath(const std::string& path) { return kVfsMount + path; }

// Creates every directory on the way to `path` (a file path).
void MakeParentDirs(const std::string& path) {
  for (size_t pos = path.find('/', 1); pos != std::string::npos;
       pos = path.find('/', pos + 1)) {
    const std::string dir = SdPath(path.substr(0, pos));
    if (mkdir(dir.c_str(), 0775) != 0 && errno != EEXIST) {
      log_w("fontcache: mkdir %s failed (%d)", dir.c_str(), errno);
    }
  }
}

bool WriteAll(int fd, const uint8_t* data, size_t len) {
  while (len > 0) {
    const ssize_t n = ::write(fd, data, len);
    if (n <= 0) return false;
    data += n;
    len -= static_cast<size_t>(n);
  }
  return true;
}

// 8-bit grey (255 = white) -> 4 bpp, high nibble first, rows not padded
// (FontDecoder::decode_bitmap_4bit expands a nibble with * 17).
void Pack4Bpp(const uint8_t* grey, size_t pixels, uint8_t* out) {
  for (size_t i = 0; i < pixels; i += 2) {
    const uint8_t hi = static_cast<uint8_t>((grey[i] + 8) / 17);
    const uint8_t lo =
        i + 1 < pixels ? static_cast<uint8_t>((grey[i + 1] + 8) / 17) : 0;
    out[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
  }
}

}  // namespace

// ---- Charset ---------------------------------------------------------------

namespace {

// Characters common in Chinese books but missing from the built-in font's
// GB2312 set, most importantly U+0020 and U+3000 (tools/ttf2bin.py skips
// empty glyphs such as spaces; U+3000 indents paragraphs). Without them,
// nearly every book forced the TTF open.
struct Range {
  uint16_t first;
  uint16_t last;
};
constexpr Range kExtraRanges[] = {
    {0x0020, 0x007E},  // ASCII (the built-in font has no space glyph)
    {0x00A0, 0x00FF},  // Latin-1 punctuation and letters
    {0x2000, 0x206F},  // General Punctuation
    {0x2100, 0x218F},  // Letterlike symbols, Roman numerals
    {0x2190, 0x21FF},  // Arrows
    {0x2460, 0x24FF},  // Enclosed alphanumerics
    {0x25A0, 0x25FF},  // Geometric shapes
    {0x2600, 0x26FF},  // Miscellaneous symbols
    {0x3000, 0x303F},  // CJK symbols and punctuation
    {0xFE30, 0xFE4F},  // CJK compatibility forms
    {0xFEFF, 0xFEFF},  // BOM / zero-width no-break space
    {0xFF00, 0xFFEF},  // Fullwidth forms
};

bool InExtraRanges(uint16_t unicode) {
  for (const Range& r : kExtraRanges) {
    if (unicode >= r.first && unicode <= r.last) return true;
  }
  return false;
}

}  // namespace

Charset::Charset(const uint8_t* bin_image, size_t size) {
  if (bin_image == nullptr || size < kHeaderSize) return;
  uint32_t count;
  memcpy(&count, bin_image, sizeof(count));
  if (count > 0x10000 || kHeaderSize + count * kEntrySize > size) return;
  index_ = bin_image + kHeaderSize;
  count_ = count;
}

size_t Charset::size() const {
  if (count_ == 0) return 0;
  size_t n = count_;
  for (const Range& r : kExtraRanges) n += r.last - r.first + 1u;
  return n;
}

uint16_t Charset::baseAt(size_t i) const {
  uint16_t unicode;
  memcpy(&unicode, index_ + i * kEntrySize, sizeof(unicode));
  return unicode;
}

bool Charset::baseContains(uint16_t unicode) const {
  size_t lo = 0;
  size_t hi = count_;
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const uint16_t u = baseAt(mid);
    if (u == unicode) return true;
    if (u < unicode) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return false;
}

bool Charset::contains(uint16_t unicode) const {
  if (count_ == 0) return false;
  return InExtraRanges(unicode) || baseContains(unicode);
}

void Charset::forEach(const std::function<void(uint16_t)>& fn) const {
  if (count_ == 0) return;
  for (size_t i = 0; i < count_; ++i) fn(baseAt(i));
  for (const Range& r : kExtraRanges) {
    for (uint32_t u = r.first; u <= r.last; ++u) {
      if (!baseContains(static_cast<uint16_t>(u))) {
        fn(static_cast<uint16_t>(u));
      }
    }
  }
}

// ---- FontCacheBuilder ------------------------------------------------------

FontCacheBuilder::~FontCacheBuilder() { cancel(); }

bool FontCacheBuilder::IsBuilding(const std::string& cache_path) {
  const auto& paths = ActivePaths();
  return std::find(paths.begin(), paths.end(), cache_path) != paths.end();
}

void FontCacheBuilder::start(TtfGlyphSource* source, const Charset* charset,
                             const std::string& cache_path,
                             const std::string& font_name,
                             const std::string& style) {
  cancel();
  source_ = source;
  charset_ = charset;
  cache_path_ = cache_path;
  tmp_path_ = cache_path + ".tmp";
  font_name_ = font_name;
  style_ = style;
  state_ = State::kPending;
}

void FontCacheBuilder::release() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  free(index_);
  index_ = nullptr;
  free(buf_);
  buf_ = nullptr;
  buf_len_ = 0;
  plan_.clear();
  plan_.shrink_to_fit();
  if (claimed_) {
    auto& paths = ActivePaths();
    paths.erase(std::remove(paths.begin(), paths.end(), cache_path_),
                paths.end());
    claimed_ = false;
  }
}

void FontCacheBuilder::cancel() {
  const bool had_file = fd_ >= 0;
  release();
  if (had_file) unlink(SdPath(tmp_path_).c_str());
  state_ = State::kIdle;
}

void FontCacheBuilder::fail(const char* what) {
  log_e("fontcache: %s failed for %s (errno %d)", what, cache_path_.c_str(),
        errno);
  cancel();
  state_ = State::kFailed;
}

bool FontCacheBuilder::begin() {
  if (IsBuilding(cache_path_)) {
    state_ = State::kWaiting;
    return true;
  }
  ActivePaths().push_back(cache_path_);
  claimed_ = true;

  // Only characters the TTF has; FreeType's cmap lookups need no I/O.
  plan_.reserve(charset_->size());
  charset_->forEach([this](uint16_t cp) {
    if (source_->findGlyph(cp) != nullptr) {
      plan_.emplace_back(source_->glyphIndex(cp), cp);
    }
  });
  plan_.shrink_to_fit();
  std::sort(plan_.begin(), plan_.end());
  if (plan_.empty()) {
    fail("empty charset");
    return false;
  }

  index_ = static_cast<CharGlyphInfo*>(
      heap_caps_calloc(plan_.size(), kEntrySize, MALLOC_CAP_SPIRAM));
  buf_ = static_cast<uint8_t*>(
      heap_caps_malloc(kWriteBufferSize, MALLOC_CAP_SPIRAM));
  if (index_ == nullptr || buf_ == nullptr) {
    fail("alloc");
    return false;
  }

  MakeParentDirs(tmp_path_);
  fd_ = ::open(SdPath(tmp_path_).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0664);
  if (fd_ < 0) {
    fail("open");
    return false;
  }
  // Placeholder for the header and index; rewritten by finish().
  memset(buf_, 0, kWriteBufferSize);
  size_t remaining = kHeaderSize + plan_.size() * kEntrySize;
  while (remaining > 0) {
    const size_t n = std::min(remaining, kWriteBufferSize);
    if (!WriteAll(fd_, buf_, n)) {
      fail("write");
      return false;
    }
    remaining -= n;
  }

  next_ = 0;
  filled_ = 0;
  data_size_ = 0;
  busy_us_ = 0;
  started_us_ = esp_timer_get_time();
  state_ = State::kRunning;
  log_i("fontcache: building %s (%u glyphs)", cache_path_.c_str(),
        static_cast<unsigned>(plan_.size()));
  return true;
}

bool FontCacheBuilder::flush() {
  if (buf_len_ == 0) return true;
  if (!WriteAll(fd_, buf_, buf_len_)) return false;
  buf_len_ = 0;
  return true;
}

bool FontCacheBuilder::renderBatch() {
  const size_t end = std::min(next_ + kBatchSize, plan_.size());
  std::vector<uint16_t> batch;
  batch.reserve(end - next_);
  for (size_t i = next_; i < end; ++i) batch.push_back(plan_[i].second);

  bool write_ok = true;
  std::vector<uint16_t> delivered;
  delivered.reserve(batch.size());
  source_->setReportStats(false);
  source_->loadGlyphs(batch, [&](const CharGlyphInfo& glyph,
                                 const uint8_t* bitmap) {
    CharGlyphInfo entry = glyph;
    entry.bitmap_offset = data_size_;
    const size_t pixels = static_cast<size_t>(glyph.bitmapW) * glyph.bitmapH;
    const size_t packed = (pixels + 1) / 2;
    if (bitmap == nullptr || pixels == 0 || packed > kWriteBufferSize) {
      entry.bitmapW = 0;
      entry.bitmapH = 0;
      entry.bitmap_size = 0;
    } else {
      if (buf_len_ + packed > kWriteBufferSize && !flush()) {
        write_ok = false;
        return;
      }
      Pack4Bpp(bitmap, pixels, buf_ + buf_len_);
      buf_len_ += packed;
      data_size_ += packed;
      entry.bitmap_size = static_cast<uint32_t>(packed);
    }
    index_[filled_++] = entry;
    delivered.push_back(glyph.unicode);
  });
  source_->setReportStats(true);

  // Glyphs FreeType failed to load keep their advance so pagination matches
  // the live TTF exactly.
  for (uint16_t cp : batch) {
    if (std::find(delivered.begin(), delivered.end(), cp) != delivered.end()) {
      continue;
    }
    const CharGlyphInfo* metrics = source_->findGlyph(cp);
    CharGlyphInfo entry{};
    entry.unicode = cp;
    entry.width = metrics != nullptr ? metrics->width : 0;
    entry.bitmap_offset = data_size_;
    index_[filled_++] = entry;
  }
  next_ = end;
  return write_ok;
}

bool FontCacheBuilder::finish() {
  if (!flush()) return false;
  std::sort(index_, index_ + filled_,
            [](const CharGlyphInfo& a, const CharGlyphInfo& b) {
              return a.unicode < b.unicode;
            });

  uint8_t header[kHeaderSize] = {};
  const uint32_t count = static_cast<uint32_t>(filled_);
  memcpy(header, &count, sizeof(count));
  header[4] = source_->fontSize();
  header[5] = kVersion4Bpp;
  strncpy(reinterpret_cast<char*>(header + kNameOffset), font_name_.c_str(),
          kFieldSize - 1);
  strncpy(reinterpret_cast<char*>(header + kStyleOffset), style_.c_str(),
          kFieldSize - 1);
  if (lseek(fd_, 0, SEEK_SET) != 0 || !WriteAll(fd_, header, kHeaderSize) ||
      !WriteAll(fd_, reinterpret_cast<const uint8_t*>(index_),
                filled_ * kEntrySize) ||
      fsync(fd_) != 0) {
    return false;
  }
  ::close(fd_);
  fd_ = -1;

  const std::string final_path = SdPath(cache_path_);
  unlink(final_path.c_str());  // FAT rename() does not replace
  if (rename(SdPath(tmp_path_).c_str(), final_path.c_str()) != 0) {
    unlink(SdPath(tmp_path_).c_str());
    return false;
  }

  const int64_t wall_ms = (esp_timer_get_time() - started_us_) / 1000;
  PERF_VALUE("fontcache.glyphs", filled_);
  PERF_VALUE("fontcache.kb", (kHeaderSize + filled_ * kEntrySize + data_size_) /
                                 1024);
  PERF_VALUE("fontcache.busy_ms", busy_us_ / 1000);
  PERF_VALUE("fontcache.wall_ms", wall_ms);
  log_i("fontcache: built %s: %u glyphs, %u KB, busy %lld ms, wall %lld ms",
        cache_path_.c_str(), static_cast<unsigned>(filled_),
        static_cast<unsigned>(data_size_ / 1024), busy_us_ / 1000, wall_ms);
  return true;
}

FontCacheBuilder::State FontCacheBuilder::step(uint32_t budget_ms) {
  if (state_ == State::kPending || state_ == State::kWaiting) {
    if (!begin() || state_ == State::kWaiting) return state_;
  }
  if (state_ != State::kRunning) return state_;

  const int64_t t0 = esp_timer_get_time();
  const int64_t deadline = t0 + static_cast<int64_t>(budget_ms) * 1000;
  while (next_ < plan_.size() && esp_timer_get_time() < deadline) {
    if (!renderBatch()) {
      fail("write");
      return state_;
    }
  }
  busy_us_ += esp_timer_get_time() - t0;
  if (next_ >= plan_.size()) {
    if (!finish()) {
      fail("finish");
      return state_;
    }
    release();
    state_ = State::kDone;
  }
  return state_;
}

}  // namespace fonts
}  // namespace reader
