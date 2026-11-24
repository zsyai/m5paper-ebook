#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_BOOK_TOC_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_BOOK_TOC_H_

// Binary table-of-contents cache (`/configs/<filename>.toc`) shared by EPUB and
// TXT books, plus the incremental background chapter scanner for TXT books.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "book/text_decode.h"

namespace reader {
namespace book {

struct TocEntry {
  uint32_t text_offset = 0;  // Byte offset in `.epc` (EPUB) or `.txt` (TXT).
  uint8_t level = 0;         // 0 = top-level, 1+ = sub-chapters.
  bool resolved = true;      // False while an EPUB spine item is still queued.
  uint16_t spine_index = 0;  // Used while `!resolved` in incremental EPUB mode.
  std::string title;
};

struct TocData {
  uint32_t source_size = 0;
  uint32_t source_mtime = 0;
  uint32_t content_size = 0;
  bool complete = false;
  std::string book_title;
  std::vector<TocEntry> entries;
};

// "/configs/<filename>.toc" and "/configs/<filename>.epc" for "/books/<name>".
std::string TocPathForBook(const std::string& book_path);
std::string EpcPathForBook(const std::string& book_path);

// Strips a trailing `.txt` or `.epub` extension (case-insensitive) from a book
// filename or basename when the stem is non-empty.
std::string StripBookExtension(const std::string& filename);

// Loads the full `.toc` cache if `source_size` and `source_mtime` match.
bool LoadBookToc(const std::string& toc_path, uint32_t expected_source_size,
                 uint32_t expected_source_mtime, TocData* out);

// Fast header-only read (`< 320 B`) used by `Bookshelf::refresh()` to retrieve
// a cached `<dc:title>` without reading the chapter list.
bool LoadBookTitleFromToc(const std::string& toc_path,
                          uint32_t expected_source_size,
                          uint32_t expected_source_mtime, std::string* title,
                          bool* is_complete = nullptr);

// Atomically writes `data` to `toc_path` via `<toc_path>.tmp`.
bool SaveBookToc(const std::string& toc_path, const TocData& data);

// Detects whether a single UTF-8 line from a plain-text book is a chapter or
// volume heading. Returns true and populates `out_level` (0 = volume/part,
// 1 = chapter/section) and trimmed `out_title`.
bool IsTxtChapterHeading(const std::string& utf8_line, uint8_t* out_level,
                         std::string* out_title);

// Scans a plain-text book in bounded idle slices (`Step(budget_ms)`) so opening
// or re-paginating a TXT book never blocks on chapter detection.
class TxtTocScanner {
 public:
  TxtTocScanner() = default;
  ~TxtTocScanner();
  TxtTocScanner(const TxtTocScanner&) = delete;
  TxtTocScanner& operator=(const TxtTocScanner&) = delete;

  bool Start(const std::string& book_path, TextEncoding encoding,
             uint32_t data_start, uint32_t file_size);
  // Advances scanning for roughly `budget_ms`. Returns true once scanning
  // completes (and writes `/configs/<filename>.toc` on clean completion).
  bool Step(uint32_t budget_ms);
  void Finish();
  void Stop();

  bool active() const { return fd_ >= 0 && !done_; }
  bool done() const { return done_; }
  bool read_error() const { return read_error_; }
  const std::vector<TocEntry>& entries() const { return entries_; }

 private:
  void ScanCompleteLines(const uint8_t* raw, size_t raw_len,
                         uint32_t base_offset, bool flush_tail);
  void NormalizeLevels();
  void SaveCompletedToc();

  int fd_ = -1;
  std::string book_path_;
  TextEncoding encoding_ = TextEncoding::UTF8;
  uint32_t data_start_ = 0;
  uint32_t file_size_ = 0;
  uint32_t source_mtime_ = 0;
  uint32_t file_pos_ = 0;

  uint8_t* buf_ = nullptr;
  std::string carry_raw_;
  uint32_t carry_offset_ = 0;

  bool done_ = true;
  bool read_error_ = false;
  std::vector<TocEntry> entries_;
};

}  // namespace book
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_BOOK_TOC_H_
