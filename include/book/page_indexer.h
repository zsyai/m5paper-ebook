#pragma once

// Builds a book's page index incrementally, a few pages per Step(), so the
// reader can show the current page first and paginate in the background.
//
// When started at `anchor == data_start`, the book is indexed strictly forward
// from the beginning. When started mid-book (`anchor > data_start`, e.g. after
// changing font size or line spacing while reading), pages are indexed both
// backward from `anchor` to `data_start` and forward from `anchor` to EOF so
// turning forward or backward from the shown page never jumps or repeats text.

#include <cstddef>
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
             const LayoutParams& layout) {
    return Start(book_path, encoding, data_start, data_start, file_size,
                 layout);
  }

  // Starts bidirectional indexing anchored at `anchor` in [data_start,
  // file_size]. Backward pages in [data_start, anchor) are indexed first so
  // prevPage() and the current page number become available quickly, followed
  // by forward pages in [anchor, file_size).
  bool Start(const std::string& book_path, TextEncoding encoding,
             uint32_t data_start, uint32_t anchor, uint32_t file_size,
             const LayoutParams& layout);

  // Advances indexing for roughly `budget_ms`. Writes the complete sorted page
  // list into `pages` and returns true once both directions finish (or returns
  // true on a read error; check read_error()).
  bool Step(const PageFontCache& cache, std::vector<uint32_t>* pages,
            uint32_t budget_ms);

  // Ensures the page immediately preceding `current_offset` has been indexed.
  void StepForPrevPage(const PageFontCache& cache, uint32_t current_offset);

  // Looks up the start offset of the page before `current_offset` among the
  // pages indexed so far. Returns false if at the start of the book or not yet
  // indexed.
  bool PrevPageOffset(uint32_t current_offset, uint32_t* prev_offset) const;

  // Upper byte bound for laying out the page at `current_offset` while the
  // index is still building (caps backward-indexed pages at the next page's
  // start offset).
  uint32_t PageEndLimit(uint32_t current_offset) const;

  // 0-based page number of `current_offset`, or -1 if backward indexing has
  // not finished yet or forward indexing has not reached `current_offset`.
  int CurrentPageIndex(uint32_t current_offset) const;

  // 0-based page number of the page containing `offset` (for arbitrary chapter
  // byte offsets), or -1 if backward indexing has not finished yet or forward
  // indexing has not passed `offset`.
  int PageIndexContaining(uint32_t offset) const;

  // Number of contiguous pages from page 0 indexed so far (0 until backward
  // indexing reaches `data_start`).
  int IndexedPageCount() const;
  uint32_t PageOffsetAt(int index) const;

  void Stop();

  bool active() const { return fd_ >= 0; }
  bool done() const { return back_done_ && fwd_done_; }
  bool backward_done() const { return back_done_; }
  // True if a read failed; the pages found so far are incomplete.
  bool read_error() const { return read_error_; }
  uint32_t anchor() const { return anchor_; }
  // Start of the next forward page to index.
  uint32_t scan_offset() const { return scan_pos_; }
  // Earliest offset reached by backward indexing (`data_start` once done).
  uint32_t backward_scan_offset() const { return back_pos_; }
  // Total time spent inside Step() since Start().
  uint32_t busy_ms() const { return busy_ms_; }
  uint32_t bytes_read() const { return bytes_read_; }

 private:
  bool EnsureBuffer(size_t want_cap);
  bool ReadExact(uint32_t offset, uint8_t* dst, size_t len);
  size_t Window(uint32_t offset, size_t want, const uint8_t** data);
  void StepBackwardOneWindow(const PageFontCache& cache);
  void StepForwardOnePage(const PageFontCache& cache);
  void ExportPages(std::vector<uint32_t>* pages) const;

  int fd_ = -1;
  uint8_t* buf_ = nullptr;
  size_t buf_cap_ = 0;
  uint32_t buf_start_ = 0;  // File offset of buf_[0] for forward Window().
  size_t buf_len_ = 0;
  uint32_t file_pos_ = 0;  // Current fd position for forward Window().
  uint32_t data_start_ = 0;
  uint32_t anchor_ = 0;
  uint32_t file_size_ = 0;

  uint32_t back_pos_ = 0;
  size_t back_min_window_ = 0;
  bool back_done_ = true;
  std::vector<uint32_t> back_pages_;  // Descending: p_{k-1}, ..., data_start_.

  uint32_t scan_pos_ = 0;
  // Whether the page at scan_pos_ starts a paragraph (paragraph indent).
  bool next_paragraph_start_ = true;
  bool fwd_done_ = true;
  std::vector<uint32_t> fwd_pages_;  // Ascending: anchor_, p_{k+1}, ...

  bool read_error_ = false;
  TextEncoding encoding_ = TextEncoding::UTF8;
  LayoutParams layout_;
  uint32_t busy_ms_ = 0;
  uint32_t bytes_read_ = 0;
};

}  // namespace book
}  // namespace reader
