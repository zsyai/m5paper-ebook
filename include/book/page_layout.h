#pragma once

// Line-breaking rules shared by page rendering, background indexing and the
// reading settings preview. There is only one implementation, so indexed
// page starts always match what the reader draws.

#include <cstddef>
#include <cstdint>
#include <string>

#include "fonts/font_buffer.h"

namespace reader {
namespace book {

struct LayoutParams {
  int text_w = 0;       // Text area width in pixels.
  int text_h = 0;       // Text area height in pixels.
  int font_size = 0;    // Glyph height in pixels.
  int line_height = 0;  // font_size * line spacing factor.
  // Replace each paragraph's leading whitespace with a 2-character indent.
  bool paragraph_indent = false;
};

// Pen position inside the text area; carried across chunks of one page.
struct LayoutCursor {
  int x = 0;
  int y = 0;
  // True while nothing visible has been laid out since the start of a
  // paragraph (the start of the text or a '\n'). Set it for a page that
  // starts a paragraph; after a full page it tells whether the next page
  // does.
  bool paragraph_start = false;
};

// Punctuation that must not start a line (kinsoku).
bool CannotStartLine(uint32_t unicode);

// Characters dropped from the start of a paragraph when indenting.
bool IsIndentSpace(uint32_t unicode);

// Lays out UTF-8 `text` starting at `cursor` using glyph widths from the font
// index (no bitmap I/O). Returns the number of bytes that fit on the page and
// sets `*page_filled` once the page is full.
//
// If `out` is set, the text to draw is appended to it: the laid-out
// characters without '\r', with '\n' at every line break and the indent
// (two U+3000) at paragraph starts.
size_t MeasurePage(const uint8_t* text, size_t len, const LayoutParams& layout,
                   const PageFontCache& cache, LayoutCursor* cursor,
                   bool* page_filled, std::string* out = nullptr);

}  // namespace book
}  // namespace reader
