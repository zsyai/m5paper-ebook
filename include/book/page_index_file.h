#pragma once

// Binary page index stored at /configs/<book file name>.idx.
//
// The header records everything the page breaks depend on, so an index built
// for a different font, size, spacing, or book file is rejected on load and
// rebuilt instead of producing wrong page breaks.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reader {
namespace book {

struct IndexKey {
  uint32_t file_size = 0;
  uint32_t data_start = 0;  // First byte after the BOM.
  uint32_t font_hash = 0;   // HashString() of the loaded font path.
  uint16_t font_size = 0;
  uint16_t line_height = 0;
  uint16_t text_w = 0;
  uint16_t text_h = 0;
  uint8_t encoding = 0;     // TextEncoding value.
  uint16_t margin = 0;      // ReadingStyle::margin.
  uint8_t paragraph_indent = 0;
  uint8_t orientation = 0;  // core::Orientation value.

  bool operator==(const IndexKey& other) const;
  bool operator!=(const IndexKey& other) const { return !(*this == other); }
};

// FNV-1a; stable across builds, used for the font path in IndexKey.
uint32_t HashString(const std::string& s);

// "/configs/<file name>.idx" for a book path like "/books/<file name>".
std::string IndexPathForBook(const std::string& book_path);

// Loads page start offsets. Returns false (and leaves `pages` empty) when the
// file is missing, in the old text format, or built with a different key.
// `mid_book_anchor` (if non-null) reports whether the index was anchored mid-
// book rather than from `data_start`.
bool LoadPageIndex(const std::string& idx_path, const IndexKey& key,
                   std::vector<uint32_t>* pages,
                   bool* mid_book_anchor = nullptr);

bool SavePageIndex(const std::string& idx_path, const IndexKey& key,
                   const std::vector<uint32_t>& pages,
                   bool mid_book_anchor = false);

// Maps a target percentage in `[0, 100]` to a 0-based page index using rounded
// `(page_count - 1)` scaling so jumping to `P%` aligns with
// `ProgressForPageIndex`.
int PageIndexForPercentage(size_t page_count, float percentage);

// Computes `[0.0, 100.0]` reading progress for `page_index` out of `page_count`
// pages (`0.0` on the first page, `100.0` on the final page).
float ProgressForPageIndex(int page_index, size_t page_count);

// Converts reading progress to the `[0, 100]` integer percentage shown on the
// bookshelf (`0` at book start, `100` only when completed, and rounded
// `[0, 99]` in between).
int RoundedBookshelfProgress(float progress, bool at_start, bool completed);

}  // namespace book
}  // namespace reader
