#pragma once

// Raw pixel access to an 8-bit grayscale sprite that honours the sprite's
// rotation.
//
// LovyanGFX drawing calls map logical coordinates through the sprite
// rotation, but the buffer itself always keeps the physical (portrait)
// layout, and pushSprite() copies it as is. Code that writes getBuffer()
// directly (glyph blits, line guides) uses this view so its pixels land
// where LovyanGFX's own drawing would put them.

#include <M5GFX.h>

#include <cstdint>

namespace reader {
namespace ui {

class CanvasPixels {
 public:
  explicit CanvasPixels(LGFX_Sprite& canvas)
      : buf_(static_cast<uint8_t*>(canvas.getBuffer())),
        width_(canvas.width()),
        height_(canvas.height()) {
    // Physical buffer size; width()/height() are already rotated.
    const int rotation = canvas.getRotation() & 3;
    const int pw = (rotation & 1) ? height_ : width_;
    const int ph = (rotation & 1) ? width_ : height_;
    // Same mapping as Panel_FrameBufferBase: rotation 1 puts the logical
    // top at the physical right edge, 2 turns it upside down, 3 puts the
    // logical top at the physical left edge.
    switch (rotation) {
      case 0:
        origin_ = 0;
        dx_ = 1;
        dy_ = pw;
        break;
      case 1:
        origin_ = pw - 1;
        dx_ = pw;
        dy_ = -1;
        break;
      case 2:
        origin_ = (ph - 1) * pw + (pw - 1);
        dx_ = -1;
        dy_ = -pw;
        break;
      default:
        origin_ = (ph - 1) * pw;
        dx_ = -pw;
        dy_ = 1;
        break;
    }
  }

  bool valid() const { return buf_ != nullptr; }
  // Logical size.
  int width() const { return width_; }
  int height() const { return height_; }
  // Pixel at logical (x, y); the caller keeps it inside width() x height().
  uint8_t* at(int x, int y) const { return buf_ + origin_ + x * dx_ + y * dy_; }
  // Pointer step for x + 1; 1 means rows are contiguous in memory.
  int dx() const { return dx_; }

 private:
  uint8_t* buf_;
  int width_;
  int height_;
  int origin_ = 0;
  int dx_ = 1;
  int dy_ = 0;
};

}  // namespace ui
}  // namespace reader
