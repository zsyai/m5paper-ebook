// Glyph source for pre-rasterized .bin font packs (see tools/ttf2bin.py).
//
// File layout (little endian):
//   u32 count | u8 size | u8 version | char[64] name | char[64] style
//   CharGlyphInfo[count]   (16 bytes each, sorted by unicode)
//   bitmap area            (glyph bitmaps, 1/4/2 bpp for version 1/2/3)
//
// Performance notes:
// - The file stays open for the lifetime of the source, so a page build does
//   not pay SD.open (~20-30 ms) every time.
// - Reads use the POSIX VFS API (open/lseek/read on /sd/...) instead of
//   Arduino File: File wraps stdio with a 4 KB buffer, so every seek + small
//   read pulled 4 KB over the (display-shared) SPI bus.
// - Glyph reads are sorted by offset and coalesced into runs, so one SD read
//   serves several glyphs and seeks only move forward.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "fonts/glyph_source.h"

namespace reader {
namespace fonts {

class BinGlyphSource : public IGlyphSource {
 public:
  BinGlyphSource() = default;
  ~BinGlyphSource() override;

  BinGlyphSource(const BinGlyphSource&) = delete;
  BinGlyphSource& operator=(const BinGlyphSource&) = delete;

  // Opens a .bin font from the SD card. Returns false if the file is missing
  // or its header / index cannot be read.
  bool openFile(const std::string& path);

  // Uses a .bin image that is already in memory (the built-in flash font).
  bool openMemory(const uint8_t* data, size_t size);

  void close();

  bool isOpen() const override { return index_ != nullptr; }
  uint8_t fontSize() const override { return font_size_; }
  const std::string& fontName() const override { return font_name_; }
  // The header "style" field (TTF glyph caches store their source key here).
  const std::string& styleTag() const { return style_; }
  const CharGlyphInfo* findGlyph(uint16_t unicode) const override;
  size_t loadGlyphs(const std::vector<uint16_t>& codepoints,
                    const GlyphSink& sink) override;

 private:
  bool loadHeaderAndIndex();
  // Reads `len` bytes at absolute file offset `offset`. Returns a pointer to
  // the data (into flash for memory fonts, else into scratch_), or nullptr.
  const uint8_t* fetch(uint32_t offset, size_t len);
  bool ensureBuffer(uint8_t** buf, size_t* cap, size_t need);
  void decode(const CharGlyphInfo& info, const uint8_t* packed, uint8_t* out);

  int fd_ = -1;  // POSIX descriptor on the SD VFS, -1 when closed
  const uint8_t* mem_ = nullptr;
  size_t mem_size_ = 0;

  CharGlyphInfo* index_ = nullptr;  // PSRAM
  uint32_t count_ = 0;
  uint32_t bitmap_base_ = 0;
  uint8_t font_size_ = 0;
  uint8_t version_ = 0;
  std::string font_name_;
  std::string style_;

  uint8_t* scratch_ = nullptr;  // PSRAM, coalesced read buffer
  size_t scratch_cap_ = 0;
  uint8_t* decoded_ = nullptr;  // PSRAM, one decoded glyph
  size_t decoded_cap_ = 0;
};

}  // namespace fonts
}  // namespace reader
