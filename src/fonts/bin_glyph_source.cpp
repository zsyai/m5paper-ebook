#include "fonts/bin_glyph_source.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "core/perf_trace.h"

namespace reader {
namespace fonts {
namespace {

constexpr uint32_t kHeaderSize = 134;  // u32 + u8 + u8 + 64 + 64
constexpr size_t kNameOffset = 6;
constexpr size_t kNameSize = 64;
constexpr size_t kStyleOffset = kNameOffset + kNameSize;

// Arduino's SD library mounts the card's FAT volume here.
constexpr char kVfsMount[] = "/sd";

// Coalescing policy: neighbouring glyphs whose bitmaps are at most kMaxGap
// bytes apart are fetched with one read, up to kMaxRun bytes per read.
constexpr uint32_t kMaxGap = 2048;
constexpr uint32_t kMaxRun = 32 * 1024;

bool IsEmpty(const CharGlyphInfo& g) {
  return g.bitmapW == 0 || g.bitmapH == 0 || g.bitmap_size == 0;
}

}  // namespace

BinGlyphSource::~BinGlyphSource() {
  close();
  free(scratch_);
  free(decoded_);
}

bool BinGlyphSource::openFile(const std::string& path) {
  PERF_SCOPE("BinGlyphSource::openFile");
  close();
  fd_ = ::open((std::string(kVfsMount) + path).c_str(), O_RDONLY);
  if (fd_ < 0) return false;
  if (!loadHeaderAndIndex()) {
    close();
    return false;
  }
  return true;
}

bool BinGlyphSource::openMemory(const uint8_t* data, size_t size) {
  close();
  mem_ = data;
  mem_size_ = size;
  if (!loadHeaderAndIndex()) {
    close();
    return false;
  }
  return true;
}

void BinGlyphSource::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  mem_ = nullptr;
  mem_size_ = 0;
  free(index_);
  index_ = nullptr;
  count_ = 0;
  bitmap_base_ = 0;
  font_size_ = 0;
  version_ = 0;
  font_name_.clear();
  style_.clear();
}

bool BinGlyphSource::loadHeaderAndIndex() {
  const uint8_t* header = fetch(0, kHeaderSize);
  if (header == nullptr) return false;

  uint32_t count;
  memcpy(&count, header, sizeof(count));
  const uint8_t size = header[4];
  const uint8_t version = header[5];
  char name[kNameSize + 1];
  memcpy(name, header + kNameOffset, kNameSize);
  name[kNameSize] = '\0';
  char style[kNameSize + 1];
  memcpy(style, header + kStyleOffset, kNameSize);
  style[kNameSize] = '\0';

  if (count == 0 || count > 0x10000 || version < 1 || version > 3) return false;

  const size_t index_bytes = count * sizeof(CharGlyphInfo);
  auto* index = static_cast<CharGlyphInfo*>(
      heap_caps_malloc(index_bytes, MALLOC_CAP_SPIRAM));
  if (index == nullptr) return false;
  const uint8_t* src = fetch(kHeaderSize, index_bytes);
  if (src == nullptr) {
    free(index);
    return false;
  }
  memcpy(index, src, index_bytes);

  index_ = index;
  count_ = count;
  font_size_ = size;
  version_ = version;
  font_name_ = name;
  style_ = style;
  bitmap_base_ = kHeaderSize + index_bytes;
  return true;
}

bool BinGlyphSource::ensureBuffer(uint8_t** buf, size_t* cap, size_t need) {
  if (*cap >= need) return true;
  auto* grown =
      static_cast<uint8_t*>(heap_caps_malloc(need, MALLOC_CAP_SPIRAM));
  if (grown == nullptr) return false;
  free(*buf);
  *buf = grown;
  *cap = need;
  return true;
}

const uint8_t* BinGlyphSource::fetch(uint32_t offset, size_t len) {
  if (mem_ != nullptr) {
    if (offset + len > mem_size_) return nullptr;
    return mem_ + offset;
  }
  if (fd_ < 0 || !ensureBuffer(&scratch_, &scratch_cap_, len)) return nullptr;
  if (::lseek(fd_, offset, SEEK_SET) != static_cast<off_t>(offset)) {
    return nullptr;
  }
  size_t done = 0;
  while (done < len) {
    const ssize_t n = ::read(fd_, scratch_ + done, len - done);
    if (n <= 0) return nullptr;
    done += n;
  }
  return scratch_;
}

const CharGlyphInfo* BinGlyphSource::findGlyph(uint16_t unicode) const {
  if (index_ == nullptr) return nullptr;
  const CharGlyphInfo* begin = index_;
  const CharGlyphInfo* end = begin + count_;
  const CharGlyphInfo* it = std::lower_bound(
      begin, end, unicode,
      [](const CharGlyphInfo& a, uint16_t b) { return a.unicode < b; });
  return (it != end && it->unicode == unicode) ? it : nullptr;
}

void BinGlyphSource::decode(const CharGlyphInfo& info, const uint8_t* packed,
                            uint8_t* out) {
  memset(out, 255, info.bitmapW * info.bitmapH);
  switch (version_) {
    case 3:
      FontDecoder::decode_bitmap_2bit(packed, info.bitmap_size, out,
                                      info.bitmapW, info.bitmapH);
      break;
    case 2:
      FontDecoder::decode_bitmap_4bit(packed, info.bitmap_size, out,
                                      info.bitmapW, info.bitmapH);
      break;
    default:
      FontDecoder::decode_bitmap_1bit(packed, info.bitmap_size, out,
                                      info.bitmapW, info.bitmapH);
      break;
  }
}

size_t BinGlyphSource::loadGlyphs(const std::vector<uint16_t>& codepoints,
                                  const GlyphSink& sink) {
  if (!isOpen()) return 0;

  // Resolve metrics; deliver empty glyphs right away (no bitmap to read).
  std::vector<const CharGlyphInfo*> pending;
  pending.reserve(codepoints.size());
  size_t delivered = 0;
  for (uint16_t cp : codepoints) {
    const CharGlyphInfo* g = findGlyph(cp);
    if (g == nullptr) continue;
    if (IsEmpty(*g)) {
      sink(*g, nullptr);
      ++delivered;
    } else {
      pending.push_back(g);
    }
  }

  // Forward-only, offset-ordered reads keep FAT seeks cheap and let
  // neighbouring glyphs share a single SD transaction.
  std::sort(pending.begin(), pending.end(),
            [](const CharGlyphInfo* a, const CharGlyphInfo* b) {
              return a->bitmap_offset < b->bitmap_offset;
            });
  pending.erase(std::unique(pending.begin(), pending.end()), pending.end());

  size_t reads = 0;
  size_t read_bytes = 0;
  int64_t read_us = 0;
  size_t i = 0;
  while (i < pending.size()) {
    const uint32_t run_start = pending[i]->bitmap_offset;
    uint32_t run_end = run_start + pending[i]->bitmap_size;
    size_t j = i + 1;
    while (j < pending.size()) {
      const CharGlyphInfo* g = pending[j];
      const uint32_t g_end = g->bitmap_offset + g->bitmap_size;
      if (g->bitmap_offset > run_end + kMaxGap) break;
      if (g_end - run_start > kMaxRun) break;
      run_end = std::max(run_end, g_end);
      ++j;
    }

    const size_t run_len = run_end - run_start;
    const int64_t t0 = esp_timer_get_time();
    const uint8_t* run = fetch(bitmap_base_ + run_start, run_len);
    read_us += esp_timer_get_time() - t0;
    ++reads;
    read_bytes += run_len;
    if (run != nullptr) {
      for (size_t k = i; k < j; ++k) {
        const CharGlyphInfo& g = *pending[k];
        if (!ensureBuffer(&decoded_, &decoded_cap_, g.bitmapW * g.bitmapH)) {
          continue;
        }
        decode(g, run + (g.bitmap_offset - run_start), decoded_);
        sink(g, decoded_);
        ++delivered;
      }
    }
    i = j;
  }

  PERF_VALUE("glyph.sd_reads", reads);
  PERF_VALUE("glyph.sd_bytes", read_bytes);
  PERF_VALUE("glyph.sd_ms", read_us / 1000);
  return delivered;
}

}  // namespace fonts
}  // namespace reader
