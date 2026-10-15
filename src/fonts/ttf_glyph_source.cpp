#include "fonts/ttf_glyph_source.h"

#include <esp32-hal-log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H

#include "core/perf_trace.h"

namespace reader {
namespace fonts {
namespace {

// Arduino's SD library mounts the card's FAT volume here.
constexpr char kVfsMount[] = "/sd";

// Small read cache for glyf data. A miss reads the request rounded out to
// whole 512 B sectors (at least kMinRead), so neighbouring glyphs often share
// one SD read. Requests that do not fit a slot go straight to FreeType.
constexpr uint32_t kSlotSize = 4 * 1024;
constexpr uint32_t kSector = 512;
constexpr uint32_t kMinRead = 2 * 1024;

uint32_t ReadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

constexpr size_t kAdvanceTableEntries = 0x10000;

FT_Library g_library = nullptr;

FT_Library Library() {
  if (g_library == nullptr && FT_Init_FreeType(&g_library) != 0) {
    log_e("FT_Init_FreeType failed");
    g_library = nullptr;
  }
  return g_library;
}

FT_Face AsFace(void* p) { return static_cast<FT_Face>(p); }

unsigned long StreamRead(FT_Stream stream, unsigned long offset,
                         unsigned char* buffer, unsigned long count) {
  auto* self = static_cast<TtfGlyphSource*>(stream->descriptor.pointer);
  return self->streamRead(offset, buffer, count);
}

void StreamClose(FT_Stream /*stream*/) {
  // The descriptor is owned and closed by TtfGlyphSource::close().
}

void* PsramAlloc(size_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
}

}  // namespace

TtfGlyphSource::~TtfGlyphSource() {
  close();
  free(decoded_);
}

void TtfGlyphSource::close() {
  if (face_ != nullptr) {
    FT_Done_Face(AsFace(face_));
    face_ = nullptr;
  }
  free(stream_);
  stream_ = nullptr;
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  for (Slot& slot : slots_) {
    free(slot.data);
    slot = Slot{};
  }
  for (Pinned& pin : pinned_) {
    free(pin.data);
    pin = Pinned{};
  }
  hmtx_ = nullptr;
  hmtx_size_ = 0;
  use_clock_ = 0;
  free(advance_px_);
  advance_px_ = nullptr;
  file_size_ = 0;
  fd_pos_ = 0;
  num_hmetrics_ = 0;
  units_per_em_ = 0;
  pixel_size_ = 0;
  ascender_px_ = 0;
  font_name_.clear();
}

bool TtfGlyphSource::openFile(const std::string& path, uint8_t pixel_size) {
  PERF_SCOPE("TtfGlyphSource::openFile");
  close();
  FT_Library library = Library();
  if (library == nullptr) return false;

  fd_ = ::open((std::string(kVfsMount) + path).c_str(), O_RDONLY);
  if (fd_ < 0) return false;
  struct stat st;
  if (fstat(fd_, &st) != 0 || st.st_size <= 0) {
    close();
    return false;
  }
  file_size_ = static_cast<uint32_t>(st.st_size);
  fd_pos_ = 0;

  bool slots_ok = true;
  for (Slot& slot : slots_) {
    slot.data = static_cast<uint8_t*>(PsramAlloc(kSlotSize));
    slots_ok = slots_ok && slot.data != nullptr;
  }
  auto* stream = static_cast<FT_Stream>(calloc(1, sizeof(FT_StreamRec)));
  advance_px_ = static_cast<uint16_t*>(heap_caps_calloc(
      kAdvanceTableEntries, sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  if (!slots_ok || stream == nullptr || advance_px_ == nullptr) {
    free(stream);
    close();
    return false;
  }
  stream->size = file_size_;
  stream->descriptor.pointer = this;
  stream->read = StreamRead;
  stream->close = StreamClose;
  stream_ = stream;

  FT_Open_Args args;
  memset(&args, 0, sizeof(args));
  args.flags = FT_OPEN_STREAM;
  args.stream = stream;
  FT_Face face = nullptr;
  FT_Error err;
  {
    PERF_SCOPE("TtfGlyphSource::FT_Open_Face");
    err = FT_Open_Face(library, &args, 0, &face);
  }
  if (err != 0) {
    log_e("FT_Open_Face(%s) failed: %d", path.c_str(), err);
    close();
    return false;
  }
  face_ = face;

  if (!FT_IS_SFNT(face) || FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0 ||
      FT_Set_Pixel_Sizes(face, 0, pixel_size) != 0 || !loadHmtx()) {
    log_e("TTF %s: unsupported font (no unicode cmap or hmtx)", path.c_str());
    close();
    return false;
  }

  pixel_size_ = pixel_size;
  ascender_px_ = static_cast<int>(face->size->metrics.ascender >> 6);
  units_per_em_ = face->units_per_EM;
  font_name_ = face->family_name != nullptr ? face->family_name : "TTF";
  if (face->style_name != nullptr && strcmp(face->style_name, "Regular") != 0) {
    font_name_ += " ";
    font_name_ += face->style_name;
  }
  PERF_VALUE("ttf.num_glyphs", face->num_glyphs);
  PERF_VALUE("ttf.file_kb", file_size_ / 1024);
  log_i("TTF opened: %s (%s) %upx, %ld glyphs", path.c_str(),
        font_name_.c_str(), pixel_size_, face->num_glyphs);
  return true;
}

bool TtfGlyphSource::loadHmtx() {
  PERF_SCOPE("TtfGlyphSource::pinMetrics");
  FT_Face face = AsFace(face_);
  auto* hhea = static_cast<TT_HoriHeader*>(FT_Get_Sfnt_Table(face, ft_sfnt_hhea));
  if (hhea == nullptr || hhea->number_Of_HMetrics == 0) return false;
  num_hmetrics_ = hhea->number_Of_HMetrics;

  // FreeType 2.4 reads hmtx (and vmtx, if present) through the stream for
  // every glyph load. Those reads jump away from the glyf data and force
  // backward seeks, which re-walk the FAT chain of a 10-25 MB file. Keeping
  // both tables in PSRAM makes glyph loading read glyf data only.
  uint8_t head[12];
  if (readAt(0, head, sizeof(head)) != sizeof(head)) return false;
  const uint16_t num_tables = static_cast<uint16_t>((head[4] << 8) | head[5]);
  std::vector<uint8_t> dir(16u * num_tables);
  if (readAt(sizeof(head), dir.data(), dir.size()) != dir.size()) return false;

  size_t pinned = 0;
  for (uint16_t i = 0; i < num_tables && pinned < kPinnedCount; ++i) {
    const uint8_t* rec = dir.data() + 16u * i;
    const uint32_t tag = ReadBe32(rec);
    if (tag != TTAG_hmtx && tag != TTAG_vmtx) continue;
    const uint32_t offset = ReadBe32(rec + 8);
    const uint32_t length = ReadBe32(rec + 12);
    if (length == 0 || offset > file_size_ || length > file_size_ - offset) {
      continue;
    }
    Pinned& pin = pinned_[pinned];
    pin.data = static_cast<uint8_t*>(PsramAlloc(length));
    if (pin.data == nullptr || readAt(offset, pin.data, length) != length) {
      free(pin.data);
      pin = Pinned{};
      continue;
    }
    pin.offset = offset;
    pin.length = length;
    ++pinned;
    if (tag == TTAG_hmtx) {
      hmtx_ = pin.data;
      hmtx_size_ = length;
    }
    PERF_VALUE(tag == TTAG_hmtx ? "ttf.hmtx_kb" : "ttf.vmtx_kb", length / 1024);
  }
  return hmtx_ != nullptr && hmtx_size_ >= 4u * num_hmetrics_;
}

uint16_t TtfGlyphSource::advanceUnits(uint32_t glyph_index) const {
  // Glyphs past numberOfHMetrics reuse the last advance.
  const uint32_t i = std::min<uint32_t>(glyph_index, num_hmetrics_ - 1u);
  const uint8_t* p = hmtx_ + 4u * i;
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint16_t TtfGlyphSource::advancePixels(uint16_t unicode) const {
  uint16_t v = advance_px_[unicode];
  if (v != kUnknown) return v;
  const FT_UInt gid = FT_Get_Char_Index(AsFace(face_), unicode);
  if (gid == 0) {
    v = kMissing;
  } else {
    const uint32_t units = advanceUnits(gid);
    const uint32_t px = (units * pixel_size_ + units_per_em_ / 2) / units_per_em_;
    v = static_cast<uint16_t>(std::min<uint32_t>(px, 0xFFFD) + 1);
  }
  advance_px_[unicode] = v;
  return v;
}

const CharGlyphInfo* TtfGlyphSource::findGlyph(uint16_t unicode) const {
  if (!isOpen()) return nullptr;
  const uint16_t v = advancePixels(unicode);
  if (v == kMissing) return nullptr;
  scratch_info_ = CharGlyphInfo{};
  scratch_info_.unicode = unicode;
  scratch_info_.width = static_cast<uint16_t>(v - 1);
  return &scratch_info_;
}

uint32_t TtfGlyphSource::glyphIndex(uint16_t unicode) const {
  if (!isOpen()) return 0;
  return FT_Get_Char_Index(AsFace(face_), unicode);
}

size_t TtfGlyphSource::readAt(uint32_t offset, uint8_t* buffer, size_t count) {
  const int64_t t0 = esp_timer_get_time();
  if (fd_pos_ != offset) {
    if (lseek(fd_, offset, SEEK_SET) != static_cast<off_t>(offset)) return 0;
    fd_pos_ = offset;
  }
  size_t got = 0;
  while (got < count) {
    const ssize_t n = ::read(fd_, buffer + got, count - got);
    if (n <= 0) break;
    got += static_cast<size_t>(n);
  }
  fd_pos_ += got;
  stat_read_us_ += esp_timer_get_time() - t0;
  ++stat_reads_;
  stat_bytes_ += got;
  return got;
}

unsigned long TtfGlyphSource::streamRead(unsigned long offset,
                                         unsigned char* buffer,
                                         unsigned long count) {
  if (count == 0) {
    // FreeType seek request: 0 means success.
    return offset <= file_size_ ? 0 : 1;
  }
  if (fd_ < 0 || offset >= file_size_) return 0;
  count = std::min<unsigned long>(count, file_size_ - offset);
  const uint32_t end = offset + count;

  for (const Pinned& pin : pinned_) {
    if (pin.data != nullptr && offset >= pin.offset &&
        end <= pin.offset + pin.length) {
      memcpy(buffer, pin.data + (offset - pin.offset), count);
      return count;
    }
  }

  for (Slot& slot : slots_) {
    if (slot.len != 0 && offset >= slot.start && end <= slot.start + slot.len) {
      slot.last_use = ++use_clock_;
      memcpy(buffer, slot.data + (offset - slot.start), count);
      return count;
    }
  }

  // Miss: read whole sectors covering the request (at least kMinRead).
  const uint32_t start = offset & ~(kSector - 1);
  uint32_t read_end = std::max(end, start + kMinRead);
  read_end = (read_end + kSector - 1) & ~(kSector - 1);
  read_end = std::min(read_end, file_size_);
  if (read_end - start > kSlotSize) {
    return readAt(offset, buffer, count);
  }

  Slot* victim = &slots_[0];
  for (Slot& slot : slots_) {
    if (slot.last_use < victim->last_use) victim = &slot;
  }
  victim->len = 0;
  const size_t got = readAt(start, victim->data, read_end - start);
  if (got < end - start) return 0;
  victim->start = start;
  victim->len = static_cast<uint32_t>(got);
  victim->last_use = ++use_clock_;
  memcpy(buffer, victim->data + (offset - start), count);
  return count;
}

size_t TtfGlyphSource::loadGlyphs(const std::vector<uint16_t>& codepoints,
                                  const GlyphSink& sink) {
  if (!isOpen()) return 0;
  FT_Face face = AsFace(face_);

  // Load in glyph index order: glyf data is usually laid out that way, so
  // the read-ahead window serves neighbouring glyphs without extra seeks.
  std::vector<std::pair<FT_UInt, uint16_t>> pending;
  pending.reserve(codepoints.size());
  for (uint16_t cp : codepoints) {
    if (advancePixels(cp) == kMissing) continue;
    pending.emplace_back(FT_Get_Char_Index(face, cp), cp);
  }
  std::sort(pending.begin(), pending.end());
  pending.erase(std::unique(pending.begin(), pending.end()), pending.end());

  stat_reads_ = 0;
  stat_bytes_ = 0;
  stat_read_us_ = 0;
  const int64_t t_start = esp_timer_get_time();
  size_t delivered = 0;
  for (const auto& entry : pending) {
    const FT_UInt gid = entry.first;
    const uint16_t cp = entry.second;
    if (FT_Load_Glyph(face, gid, FT_LOAD_NO_HINTING | FT_LOAD_RENDER) != 0) {
      continue;
    }
    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bm = slot->bitmap;

    CharGlyphInfo info{};
    info.unicode = cp;
    info.width = static_cast<uint16_t>(advance_px_[cp] - 1);
    const int w = std::min<int>(bm.width, 255);
    const int h = std::min<int>(bm.rows, 255);
    if (w <= 0 || h <= 0 || bm.buffer == nullptr) {
      sink(info, nullptr);
      ++delivered;
      continue;
    }
    info.bitmapW = static_cast<uint8_t>(w);
    info.bitmapH = static_cast<uint8_t>(h);
    info.x_offset = static_cast<int8_t>(
        std::max(-128, std::min(127, static_cast<int>(slot->bitmap_left))));
    info.y_offset = static_cast<int8_t>(std::max(
        -128, std::min(127, ascender_px_ - static_cast<int>(slot->bitmap_top))));
    info.bitmap_size = static_cast<uint32_t>(w * h);

    if (decoded_cap_ < info.bitmap_size) {
      free(decoded_);
      decoded_ = static_cast<uint8_t*>(PsramAlloc(info.bitmap_size));
      decoded_cap_ = decoded_ != nullptr ? info.bitmap_size : 0;
      if (decoded_ == nullptr) continue;
    }
    // FreeType coverage is 255 = ink; the reader's bitmaps use 255 = white.
    for (int y = 0; y < h; ++y) {
      const uint8_t* src = bm.buffer + y * bm.pitch;
      uint8_t* dst = decoded_ + y * w;
      for (int x = 0; x < w; ++x) dst[x] = static_cast<uint8_t>(255 - src[x]);
    }
    sink(info, decoded_);
    ++delivered;
  }
  const int64_t total_us = esp_timer_get_time() - t_start;

  if (report_stats_) {
    PERF_VALUE("glyph.sd_reads", stat_reads_);
    PERF_VALUE("glyph.sd_bytes", stat_bytes_);
    PERF_VALUE("glyph.sd_ms", stat_read_us_ / 1000);
    PERF_VALUE("glyph.raster_ms", (total_us - stat_read_us_) / 1000);
  }
  return delivered;
}

}  // namespace fonts
}  // namespace reader
