#pragma once

// Hardware-independent 8-bit grayscale -> IT8951 4bpp big-endian packing for
// reading page refreshes. Kept free of M5GFX headers so host unit tests
// ([env:native]) can verify nibble order and linear 16-level tone mapping.

#include <cstddef>
#include <cstdint>

namespace reader {
namespace hal {

// Populates `lut[0..256)` with 4-bit IT8951 grayscale indices (`0` = black,
// `15` = white; inverted when `invert` is true) via linear `g >> 4` (exact
// inverse of `nibble * 17` 4bpp glyph caches, without ordered Bayer noise).
inline void BuildReadingGrayLut(bool invert, uint8_t lut[256]) {
  for (int g = 0; g < 256; ++g) {
    const uint8_t nibble = static_cast<uint8_t>(g >> 4);
    lut[g] = invert ? static_cast<uint8_t>(nibble ^ 0x0Fu) : nibble;
  }
}

// Packs `pixel_count` (must be even) 8-bit grayscale pixels from `src` into
// `pixel_count / 2` bytes at `dst` in IT8951 `LDIMG_B_ENDIAN` 4bpp order:
// the left pixel occupies bits 7..4 and the right pixel occupies bits 3..0,
// matching `Panel_IT8951`'s `getSwap16((p0 << 12) | (p1 << 8) | (p2 << 4) |
// p3)`.
inline void PackGray8To4bppBe(const uint8_t* src, size_t pixel_count,
                              const uint8_t lut[256], uint8_t* dst) {
  size_t pairs = pixel_count >> 1;
  while (pairs >= 4) {
    dst[0] = static_cast<uint8_t>((lut[src[0]] << 4) | lut[src[1]]);
    dst[1] = static_cast<uint8_t>((lut[src[2]] << 4) | lut[src[3]]);
    dst[2] = static_cast<uint8_t>((lut[src[4]] << 4) | lut[src[5]]);
    dst[3] = static_cast<uint8_t>((lut[src[6]] << 4) | lut[src[7]]);
    src += 8;
    dst += 4;
    pairs -= 4;
  }
  while (pairs-- > 0) {
    *dst++ = static_cast<uint8_t>((lut[src[0]] << 4) | lut[src[1]]);
    src += 2;
  }
}

}  // namespace hal
}  // namespace reader
