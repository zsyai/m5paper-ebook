// Glyph source abstraction for the fonts layer.
//
// Namespaced as reader::fonts because M5GFX declares a global `fonts` alias.
//
// A glyph source owns one font face at one pixel size and can:
//   - answer metric lookups without I/O (used by pagination), and
//   - decode a batch of glyph bitmaps as efficiently as its storage allows.
//
// Implementations: BinGlyphSource (pre-rasterized .bin packs, tools/ttf2bin).
// A TTF-backed source will implement the same interface in Phase 2.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fonts/font_decoder.h"  // CharGlyphInfo

namespace reader {
namespace fonts {

// Receives one glyph. `bitmap` is 8-bit grayscale (255 = white), sized
// info.bitmapW * info.bitmapH, or nullptr for empty glyphs such as spaces.
// The bitmap is only valid for the duration of the call.
using GlyphSink =
    std::function<void(const CharGlyphInfo& info, const uint8_t* bitmap)>;

class IGlyphSource {
 public:
  virtual ~IGlyphSource() = default;

  virtual bool isOpen() const = 0;
  virtual uint8_t fontSize() const = 0;
  virtual const std::string& fontName() const = 0;

  // Metrics for `unicode`, or nullptr if the font has no such glyph. No I/O.
  virtual const CharGlyphInfo* findGlyph(uint16_t unicode) const = 0;

  // Decodes the glyphs for `codepoints` (any order, duplicates allowed) and
  // calls `sink` once per glyph present in the font. Returns the number of
  // glyphs delivered.
  virtual size_t loadGlyphs(const std::vector<uint16_t>& codepoints,
                            const GlyphSink& sink) = 0;
};

}  // namespace fonts
}  // namespace reader
