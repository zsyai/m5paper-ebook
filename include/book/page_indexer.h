#pragma once

// Builds a book's page index incrementally, a few pages per Step(), so the
// reader can show the current page first and paginate in the background.
//
// The book is read strictly forward through a large POSIX read window, which
// is several times faster on the M5Paper SD card than a seek + 4 KB read per
// page (FatFS fast seek is off, and backward seeks re-walk the FAT chain).

#include <cstdint>
#include <string>
#include <vector>

#include "book/page_layout.h"
#include "book/text_decode.h"
#include "fonts/font_buffer.h"

namespace reader {
namespace book {

class PageIndexer {
 public:
  PageIndexer() = default;
  ~PageIndexer();
  PageIndexer(const PageIndexer&) = delete;
  PageIndexer& operator=(const PageIndexer&) = delete;

  // Opens `book_path` (an SD path like "/books/x.txt") and starts indexing
  // from `data_start`. Any previous run is stopped.
  bool Start(const std::string& book_path, TextEncoding encoding,
             uint32_t data_start, uint32_t file_size,
             const LayoutParams& layout);

  // Appends page start offsets to `pages` for roughly `budget_ms`. Returns
  // true once the whole book is indexed (or on a read error, which ends the
  // run with what was indexed so far).
  bool Step(const PageFontCache& cache, std::vector<uint32_t>* pages,
            uint32_t budget_ms);

  void Stop();

  bool active() const { return fd_ >= 0; }
  bool done() const { return done_; }
  // Start of the next page to index; every page before it is in `pages`.
  uint32_t scan_offset() const { return scan_pos_; }
  // Total time spent inside Step() since Start().
  uint32_t busy_ms() const { return busy_ms_; }
  uint32_t bytes_read() const { return bytes_read_; }

 private:
  // Makes [offset, offset + want) available in the window, or as much of it
  // as the file has. Returns the number of bytes available at `*data`.
  size_t Window(uint32_t offset, size_t want, const uint8_t** data);

  int fd_ = -1;
  uint8_t* buf_ = nullptr;
  uint32_t buf_start_ = 0;  // File offset of buf_[0].
  size_t buf_len_ = 0;
  uint32_t file_pos_ = 0;   // Current fd position (end of the window).
  uint32_t file_size_ = 0;
  uint32_t scan_pos_ = 0;
  // Whether the page at scan_pos_ starts a paragraph (paragraph indent).
  bool next_paragraph_start_ = true;
  bool done_ = false;
  bool read_error_ = false;
  TextEncoding encoding_ = TextEncoding::UTF8;
  LayoutParams layout_;
  uint32_t busy_ms_ = 0;
  uint32_t bytes_read_ = 0;
};

}  // namespace book
}  // namespace reader
