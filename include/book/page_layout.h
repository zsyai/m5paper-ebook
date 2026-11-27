#pragma once

// Line-breaking rules shared by page rendering, background indexing and the
// reading settings preview. There is only one implementation, so indexed
// page starts always match what the reader draws.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

// Prefix of a standalone EPUB illustration line in `.epc` ("\\x1EIMG:<idx>\\n").
constexpr uint8_t kImagePageSentinel = 0x1E;
constexpr char kImagePagePrefix[] = "\x1EIMG:";

// Parses a standalone illustration page marker produced by `MeasurePage`,
// returning true and storing the 0-based ZIP entry index on match.
bool ParseImagePageMarker(const std::string& text, uint32_t* zip_entry_index);

// Lays out UTF-8 `text` starting at `cursor` using glyph widths from the font
// index (no bitmap I/O). Returns the number of bytes that fit on the page and
// sets `*page_filled` once the page is full.
//
// If `out` is set, the text to draw is appended to it: the laid-out
// characters without '\r', with '\n' at every line break ("-\n" where a
// word is hyphenated) and the indent (two U+3000) at paragraph starts.
//
// Lines break between words; a word that does not fit is hyphenated at a
// French syllable boundary when possible. Set `more_follows` when `text` is
// a chunk and more text follows: a word cut by the chunk end is then left for
// the next call (the return value stops at its start).
size_t MeasurePage(const uint8_t* text, size_t len, const LayoutParams& layout,
                   const PageFontCache& cache, LayoutCursor* cursor,
                   bool* page_filled, std::string* out = nullptr,
                   bool more_follows = false);

struct LineSpan {
  // Byte offset in `text` where starting MeasurePage reproduces this line and
  // all following lines.
  size_t utf8_start;
  // True if at least one visible character or paragraph indent sits on this
  // line (the positions where MeasurePage checks text_h).
  bool has_visible;
  // False only in the degenerate case where a 2-em indent wraps before the
  // paragraph's first character, so `utf8_start` still sits right after '\n'.
  bool can_start_page;
  // True when this span is a standalone full-page item (e.g. an EPUB image
  // marker) that cannot share a page with preceding or following lines.
  bool standalone_page;
};

// Wraps UTF-8 `text` into visual lines with the same line-breaking rules as
// MeasurePage (ignoring text_h), used to paginate backward from a mid-book
// anchor in a single linear pass per window.
void MeasureLines(const uint8_t* text, size_t len, const LayoutParams& layout,
                  const PageFontCache& cache, bool start_is_paragraph,
                  std::vector<LineSpan>* lines);

}  // namespace book
}  // namespace reader
