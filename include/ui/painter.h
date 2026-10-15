#pragma once

// Canvas + UI glyph cache pair used by pages and widgets to draw UI text.

#include <M5GFX.h>

#include <string>

#include "fonts/font_buffer.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

class UiPainter {
 public:
  UiPainter(LGFX_Sprite& canvas, PageFontCache& cache)
      : canvas_(canvas), cache_(cache) {}

  UiPainter(const UiPainter&) = delete;
  UiPainter& operator=(const UiPainter&) = delete;

  // UI font path ("builtin" or a .ttf); set when the UI font changes.
  void set_font_path(const std::string& path) { font_path_ = path; }
  const std::string& font_path() const { return font_path_; }

  LGFX_Sprite& canvas() { return canvas_; }
  PageFontCache& cache() { return cache_; }
  int font_size() const { return cache_.getFontSize(); }

  // Loads the glyphs of `text` into the cache.
  void Prepare(const std::string& text);
  // Width of already prepared text.
  int Width(const std::string& text);
  // Draws prepared text with its top-left at (x, y); lines are
  // 1.5 x font size apart. `inverted` draws white text on black.
  void Text(const std::string& text, int x, int y, bool inverted = false);
  // Prepares and draws one line of text centered in `r`.
  void TextIn(const std::string& text, const Rect& r, bool inverted = false);
  // Top y that centers one line of text vertically in [top, top + h).
  int CenterY(int top, int h) const { return top + (h - font_size()) / 2; }

 private:
  LGFX_Sprite& canvas_;
  PageFontCache& cache_;
  std::string font_path_ = "builtin";
};

}  // namespace ui
}  // namespace reader
