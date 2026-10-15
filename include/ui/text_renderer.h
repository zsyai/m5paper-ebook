#pragma once

// Draws and measures UTF-8 text with glyphs from a PageFontCache.
//
// Callers build() the cache for the text first; these functions only read
// cached glyphs. A character without a glyph advances by half the font size
// for ' ' and by the full font size otherwise, in both drawing and
// measuring.

#include <M5GFX.h>

#include <string>

#include "fonts/font_buffer.h"

namespace reader {
namespace ui {

// Copies glyph bitmaps into the 8-bit grayscale `canvas`, top-left of the
// first line at (x, y). '\n' starts a new line `line_height` lower.
// Glyph boxes overwrite the background, so draw on white, or pass
// `inverted` to draw white text on black.
void DrawText(LGFX_Sprite& canvas, PageFontCache& cache,
              const std::string& text, int x, int y, int line_height,
              bool inverted = false);

// Dashed grey line under every text line of an area of `w` x `h` pixels at
// (x, y) laid out with `font_size` glyphs every `line_height` pixels, i.e.
// every line MeasurePage() can place there, filled or not. The dashes only
// darken pixels, so draw them after the text.
void DrawLineGuides(LGFX_Sprite& canvas, int x, int y, int w, int h,
                    int font_size, int line_height);

// Width of one line of text.
int TextWidth(PageFontCache& cache, const std::string& text);

// Returns `text` if it fits in `max_w`, else the longest prefix (whole
// UTF-8 characters) that fits with "..." appended, plus the "...".
// The glyphs of `text` and "." must already be cached.
std::string Ellipsize(PageFontCache& cache, std::string text, int max_w);

}  // namespace ui
}  // namespace reader
