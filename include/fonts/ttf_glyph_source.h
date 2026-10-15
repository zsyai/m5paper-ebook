// Glyph source that rasterizes TrueType (glyf) fonts on the device.
//
// Uses the FreeType 2.4 copy bundled with OpenFontRender (TrueType driver,
// sfnt, smooth renderer; no CFF). The font file is streamed from the SD card
// instead of being loaded into PSRAM, because CJK fonts are 10-25 MB.
//
// Performance notes:
// - FreeType reads go through a custom FT_Stream backed by a POSIX
//   descriptor and a few small sector-aligned cache slots. Glyphs are loaded
//   in glyph index order (roughly glyf offset order), so seeks move forward.
// - FreeType keeps cmap and loca in memory but reads hmtx/vmtx through the
//   stream on every glyph load. Those tables are pinned in PSRAM at open, so
//   glyph loads only touch glyf data and findGlyph() does no I/O.
// - Glyphs are rendered unhinted (the bytecode interpreter is slow and needs
//   a deep stack).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "fonts/glyph_source.h"

namespace reader {
namespace fonts {

class TtfGlyphSource : public IGlyphSource {
 public:
  TtfGlyphSource() = default;
  ~TtfGlyphSource() override;

  TtfGlyphSource(const TtfGlyphSource&) = delete;
  TtfGlyphSource& operator=(const TtfGlyphSource&) = delete;

  // Opens a .ttf from the SD card at `pixel_size`. Returns false if the file
  // is missing or is not a TrueType font FreeType can load.
  bool openFile(const std::string& path, uint8_t pixel_size);

  void close();

  bool isOpen() const override { return face_ != nullptr; }
  uint8_t fontSize() const override { return pixel_size_; }
  const std::string& fontName() const override { return font_name_; }
  const CharGlyphInfo* findGlyph(uint16_t unicode) const override;
  size_t loadGlyphs(const std::vector<uint16_t>& codepoints,
                    const GlyphSink& sink) override;

  // Glyph index of `unicode` (0 if missing). Glyph data is usually stored
  // in glyph index order, so loading in this order keeps reads forward.
  uint32_t glyphIndex(uint16_t unicode) const;

  // loadGlyphs() logs SD / raster perf values unless disabled (the font
  // cache builder calls it thousands of times in the background).
  void setReportStats(bool report) { report_stats_ = report; }

  // Stream callback target; public only so the C callback can reach it.
  unsigned long streamRead(unsigned long offset, unsigned char* buffer,
                           unsigned long count);

 private:
  // Special values of advance_px_ entries.
  static constexpr uint16_t kUnknown = 0;
  static constexpr uint16_t kMissing = 0xFFFF;

  static constexpr size_t kPinnedCount = 2;  // hmtx, vmtx
  static constexpr size_t kSlots = 4;

  struct Pinned {
    uint32_t offset = 0;
    uint32_t length = 0;
    uint8_t* data = nullptr;  // PSRAM
  };
  struct Slot {
    uint32_t start = 0;
    uint32_t len = 0;  // 0 = empty
    uint32_t last_use = 0;
    uint8_t* data = nullptr;  // PSRAM
  };

  // Pins hmtx (required) and vmtx (optional) in PSRAM.
  bool loadHmtx();
  // Reads `count` bytes at `offset` straight from the file.
  size_t readAt(uint32_t offset, uint8_t* buffer, size_t count);
  uint16_t advanceUnits(uint32_t glyph_index) const;
  uint16_t advancePixels(uint16_t unicode) const;

  // FreeType objects, stored opaquely to keep FreeType headers out of here.
  void* face_ = nullptr;    // FT_Face
  void* stream_ = nullptr;  // FT_StreamRec*

  int fd_ = -1;
  uint32_t file_size_ = 0;
  uint32_t fd_pos_ = 0;  // current POSIX file position

  Slot slots_[kSlots];
  uint32_t use_clock_ = 0;
  Pinned pinned_[kPinnedCount];

  // hmtx (points into pinned_) and header values.
  const uint8_t* hmtx_ = nullptr;
  uint32_t hmtx_size_ = 0;
  uint16_t num_hmetrics_ = 0;
  uint16_t units_per_em_ = 0;

  // Memoized advance per BMP codepoint, stored as pixels + 1 (PSRAM).
  mutable uint16_t* advance_px_ = nullptr;
  // findGlyph() result; valid until the next call.
  mutable CharGlyphInfo scratch_info_{};

  uint8_t pixel_size_ = 0;
  int ascender_px_ = 0;
  std::string font_name_;

  uint8_t* decoded_ = nullptr;  // PSRAM, one glyph as 8-bit grayscale
  size_t decoded_cap_ = 0;

  // Stats for the current loadGlyphs() call.
  bool report_stats_ = true;
  uint32_t stat_reads_ = 0;
  uint32_t stat_bytes_ = 0;
  int64_t stat_read_us_ = 0;
};

}  // namespace fonts
}  // namespace reader
