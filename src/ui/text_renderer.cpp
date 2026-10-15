#include "ui/text_renderer.h"

#include <algorithm>
#include <cstring>

#include "ui/canvas_pixels.h"
#include "book/utf8_utils.h"

namespace reader {
namespace ui {

namespace {

// Advance of a character: the glyph width, or the fallback for a missing
// glyph (half the font size for ' ', the full size otherwise).
int Advance(PageFontCache& cache, uint16_t u, const CharGlyphInfo* info) {
  if (info) return info->width;
  return (u == ' ') ? cache.getFontSize() / 2 : cache.getFontSize();
}

// Characters that DrawText skips without advancing.
bool IsSkipped(uint32_t unicode) {
  return unicode == 0 || unicode > 0xFFFF || unicode == '\r';
}

}  // namespace

void DrawText(LGFX_Sprite& canvas, PageFontCache& cache,
              const std::string& text, int x, int y, int line_height,
              bool inverted) {
  const CanvasPixels pixels(canvas);
  if (!pixels.valid()) return;
  const int canvas_w = pixels.width();
  const int canvas_h = pixels.height();
  const int step = pixels.dx();

  int cursor_x = x;
  int cursor_y = y;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
  const uint8_t* end = p + text.size();
  while (p < end) {
    int bytes = 0;
    const uint32_t unicode = Utf8Utils::getNextCharUnicode(p, end, bytes);
    if (bytes == 0) {
      p++;
      continue;
    }
    p += bytes;
    if (IsSkipped(unicode)) continue;
    if (unicode == '\n') {
      cursor_x = x;
      cursor_y += line_height;
      continue;
    }

    const uint16_t u = static_cast<uint16_t>(unicode);
    const CharGlyphInfo* info = cache.getCharGlyphInfo(u);
    const uint8_t* bitmap = info ? cache.getCharBitmap(u) : nullptr;
    if (bitmap) {
      for (int row = 0; row < info->bitmapH; ++row) {
        const int dst_y = cursor_y + info->y_offset + row;
        if (dst_y < 0 || dst_y >= canvas_h) continue;
        int dst_x = cursor_x + info->x_offset;
        int copy_w = info->bitmapW;
        int src_x = 0;
        if (dst_x < 0) {
          src_x = -dst_x;
          copy_w += dst_x;
          dst_x = 0;
        }
        if (dst_x + copy_w > canvas_w) copy_w = canvas_w - dst_x;
        if (copy_w <= 0) continue;
        uint8_t* out = pixels.at(dst_x, dst_y);
        const uint8_t* in = bitmap + row * info->bitmapW + src_x;
        if (inverted) {
          for (int i = 0; i < copy_w; ++i) out[i * step] = 255 - in[i];
        } else if (step == 1) {
          memcpy(out, in, copy_w);
        } else {
          for (int i = 0; i < copy_w; ++i) out[i * step] = in[i];
        }
      }
    }
    cursor_x += Advance(cache, u, info);
  }
}

void DrawLineGuides(LGFX_Sprite& canvas, int x, int y, int w, int h,
                    int font_size, int line_height) {
  constexpr uint8_t kGray = 0xA0;
  constexpr int kDash = 6;
  constexpr int kGap = 4;
  const CanvasPixels pixels(canvas);
  if (!pixels.valid() || line_height <= 0 || h < font_size) return;
  const int canvas_w = pixels.width();
  const int canvas_h = pixels.height();
  const int x0 = std::max(x, 0);
  const int x1 = std::min(x + w, canvas_w);
  // Midway through the gap under the glyphs, at least 1px below them.
  const int below = std::max(1, (line_height - font_size) / 2);
  for (int top = y; top + font_size <= y + h; top += line_height) {
    const int line_y = top + font_size + below;
    if (line_y < 0 || line_y >= canvas_h) continue;
    for (int px = x0; px < x1; ++px) {
      if ((px - x) % (kDash + kGap) >= kDash) continue;
      uint8_t* dot = pixels.at(px, line_y);
      *dot = std::min(*dot, kGray);
    }
  }
}

int TextWidth(PageFontCache& cache, const std::string& text) {
  int width = 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
  const uint8_t* end = p + text.size();
  while (p < end) {
    int bytes = 0;
    const uint32_t unicode = Utf8Utils::getNextCharUnicode(p, end, bytes);
    if (bytes == 0) {
      p++;
      continue;
    }
    p += bytes;
    if (IsSkipped(unicode) || unicode == '\n') continue;
    const uint16_t u = static_cast<uint16_t>(unicode);
    width += Advance(cache, u, cache.getCharGlyphInfo(u));
  }
  return width;
}

std::string Ellipsize(PageFontCache& cache, std::string text, int max_w) {
  if (TextWidth(cache, text) <= max_w) return text;
  while (!text.empty() && TextWidth(cache, text + "...") > max_w) {
    // Drop one whole UTF-8 character.
    uint8_t c;
    do {
      c = static_cast<uint8_t>(text.back());
      text.pop_back();
    } while (!text.empty() && (c & 0xC0) == 0x80);
  }
  return text + "...";
}

}  // namespace ui
}  // namespace reader
