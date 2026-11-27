#include "book/page_indexer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <functional>

#include "core/sd_path.h"
#include "hal/storage.h"

namespace reader {
namespace book {
namespace {

// Raw bytes laid out per forward attempt; matches BookReader::paginate.
constexpr size_t kChunkSize = 4096;
// Default sequential read window (~1.1-1.2 MB/s in 32 KB requests on M5Paper).
constexpr size_t kWindowSize = 32 * 1024;
// Upper cap when expanding the backward window to enclose a newline-free block.
constexpr size_t kMaxBackWindowSize = 256 * 1024;

size_t FindNewlineEnd(const uint8_t* data, size_t search_len,
                      TextEncoding enc) {
  if (enc == TextEncoding::UTF16LE) {
    for (size_t i = 0; i + 1 < search_len; i += 2) {
      if (data[i] == '\n' && data[i + 1] == 0) return i + 2;
    }
    return 0;
  }
  if (enc == TextEncoding::UTF16BE) {
    for (size_t i = 0; i + 1 < search_len; i += 2) {
      if (data[i] == 0 && data[i + 1] == '\n') return i + 2;
    }
    return 0;
  }
  for (size_t i = 0; i < search_len; ++i) {
    if (data[i] == '\n') return i + 1;
  }
  return 0;
}

size_t StepGbkChar(const uint8_t* buf, size_t pos, size_t end) {
  const uint8_t c = buf[pos];
  if (c >= 0x81 && c <= 0xFE && pos + 3 < end && buf[pos + 1] >= 0x30 &&
      buf[pos + 1] <= 0x39 && buf[pos + 2] >= 0x81 && buf[pos + 2] <= 0xFE &&
      buf[pos + 3] >= 0x30 && buf[pos + 3] <= 0x39) {
    return 4;
  }
  if (c >= 0x81 && c <= 0xFE && pos + 1 < end) return 2;
  return 1;
}

}  // namespace

PageIndexer::~PageIndexer() { Stop(); }

bool PageIndexer::EnsureBuffer(size_t want_cap) {
  if (buf_ && buf_cap_ >= want_cap) return true;
  uint8_t* next = static_cast<uint8_t*>(
      heap_caps_malloc(want_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!next) next = static_cast<uint8_t*>(malloc(want_cap));
  if (!next) return false;
  free(buf_);
  buf_ = next;
  buf_cap_ = want_cap;
  buf_start_ = 0;
  buf_len_ = 0;
  file_pos_ = 0;
  return true;
}

bool PageIndexer::ReadExact(uint32_t offset, uint8_t* dst, size_t len) {
  reader::hal::CheckSdTaskInvariant("PageIndexer::ReadExact");
  buf_start_ = 0;
  buf_len_ = 0;
  file_pos_ = 0;
  if (::lseek(fd_, offset, SEEK_SET) < 0) {
    read_error_ = true;
    reader::hal::ReportSdError();
    return false;
  }
  size_t got = 0;
  while (got < len) {
    const ssize_t n = ::read(fd_, dst + got, len - got);
    if (n <= 0) {
      read_error_ = true;
      reader::hal::ReportSdError();
      return false;
    }
    got += static_cast<size_t>(n);
    bytes_read_ += static_cast<uint32_t>(n);
  }
  return true;
}

bool PageIndexer::Start(const std::string& book_path, TextEncoding encoding,
                        uint32_t data_start, uint32_t anchor,
                        uint32_t file_size, const LayoutParams& layout) {
  reader::hal::CheckSdTaskInvariant("PageIndexer::Start");
  Stop();
  if (!EnsureBuffer(kWindowSize)) return false;
  const int fd = ::open(core::SdPath(book_path).c_str(), O_RDONLY);
  if (fd < 0) return false;

  encoding_ = encoding;
  data_start_ = std::min(data_start, file_size);
  anchor_ = std::max(data_start_, std::min(anchor, file_size));
  file_size_ = file_size;
  layout_ = layout;

  buf_start_ = 0;
  buf_len_ = 0;
  file_pos_ = 0;
  read_error_ = false;
  busy_ms_ = 0;
  bytes_read_ = 0;

  back_pages_.clear();
  back_pos_ = anchor_;
  back_min_window_ = kWindowSize;
  back_done_ = (anchor_ <= data_start_);

  fwd_pages_.clear();
  scan_pos_ = anchor_;
  fwd_done_ = (anchor_ >= file_size_);
  next_paragraph_start_ = true;
  fd_ = fd;

  if (!fwd_done_ && anchor_ > data_start_) {
    const bool utf16 = (encoding_ == TextEncoding::UTF16LE ||
                        encoding_ == TextEncoding::UTF16BE);
    const uint32_t unit = utf16 ? 2 : 1;
    if (anchor_ - data_start_ >= unit) {
      uint8_t prev[2] = {0, 0};
      if (ReadExact(anchor_ - unit, prev, unit)) {
        if (!utf16) {
          next_paragraph_start_ = (prev[0] == '\n');
        } else if (encoding_ == TextEncoding::UTF16LE) {
          next_paragraph_start_ = (prev[0] == '\n' && prev[1] == 0);
        } else {
          next_paragraph_start_ = (prev[0] == 0 && prev[1] == '\n');
        }
      } else {
        Stop();
        return false;
      }
    }
  }
  return true;
}

void PageIndexer::Stop() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (buf_) {
    free(buf_);
    buf_ = nullptr;
    buf_cap_ = 0;
  }
  back_pages_.clear();
  fwd_pages_.clear();
}

size_t PageIndexer::Window(uint32_t offset, size_t want, const uint8_t** data) {
  reader::hal::CheckSdTaskInvariant("PageIndexer::Window");
  if (!EnsureBuffer(kWindowSize)) return 0;
  if (buf_len_ == 0 || offset < buf_start_ || offset > buf_start_ + buf_len_) {
    if (::lseek(fd_, offset, SEEK_SET) < 0) {
      read_error_ = true;
      reader::hal::ReportSdError();
      return 0;
    }
    file_pos_ = offset;
    buf_start_ = offset;
    buf_len_ = 0;
  }

  size_t have = buf_start_ + buf_len_ - offset;
  if (have < want && file_pos_ < file_size_) {
    memmove(buf_, buf_ + (offset - buf_start_), have);
    buf_start_ = offset;
    buf_len_ = have;
    while (buf_len_ < kWindowSize && file_pos_ < file_size_) {
      const ssize_t n = ::read(fd_, buf_ + buf_len_, kWindowSize - buf_len_);
      if (n <= 0) {
        if (n < 0) {
          read_error_ = true;
          reader::hal::ReportSdError();
        }
        file_size_ = file_pos_;
        break;
      }
      buf_len_ += static_cast<size_t>(n);
      file_pos_ += static_cast<uint32_t>(n);
      bytes_read_ += static_cast<uint32_t>(n);
    }
    have = buf_len_;
  }

  *data = buf_ + (offset - buf_start_);
  return have < want ? have : want;
}

void PageIndexer::StepBackwardOneWindow(const PageFontCache& cache) {
  if (back_done_ || read_error_) return;
  if (back_pos_ <= data_start_) {
    back_done_ = true;
    return;
  }

  size_t cap = std::max(back_min_window_, kWindowSize);
  uint32_t win_start = data_start_;
  const uint8_t* slice = nullptr;
  size_t slice_len = 0;
  bool start_is_paragraph = true;

  while (true) {
    const uint32_t max_span = back_pos_ - data_start_;
    if (max_span <= cap) {
      if (!EnsureBuffer(max_span) || !ReadExact(data_start_, buf_, max_span)) {
        return;
      }
      win_start = data_start_;
      slice = buf_;
      slice_len = max_span;
      start_is_paragraph = true;
      break;
    }

    uint32_t raw_start = back_pos_ - static_cast<uint32_t>(cap);
    if ((encoding_ == TextEncoding::UTF16LE ||
         encoding_ == TextEncoding::UTF16BE) &&
        ((raw_start - data_start_) & 1)) {
      ++raw_start;
    }
    const size_t len = back_pos_ - raw_start;
    if (!EnsureBuffer(len) || !ReadExact(raw_start, buf_, len)) return;

    const size_t nl_end = FindNewlineEnd(buf_, len / 2, encoding_);
    if (nl_end > 0) {
      win_start = raw_start + static_cast<uint32_t>(nl_end);
      slice = buf_ + nl_end;
      slice_len = len - nl_end;
      start_is_paragraph = true;
      break;
    }

    if (cap < kMaxBackWindowSize) {
      cap = std::min(cap * 2, kMaxBackWindowSize);
      continue;
    }

    // Fallback for a >128 KB block with no newline: align to a valid character
    // boundary near raw_start.
    size_t j = 0;
    if (encoding_ == TextEncoding::UTF8) {
      while (j < len && (buf_[j] & 0xC0) == 0x80) ++j;
    } else if (encoding_ == TextEncoding::UTF16LE && len >= 2) {
      const uint16_t w = buf_[0] | (buf_[1] << 8);
      if (w >= 0xDC00 && w <= 0xDFFF) j = 2;
    } else if (encoding_ == TextEncoding::UTF16BE && len >= 2) {
      const uint16_t w = (buf_[0] << 8) | buf_[1];
      if (w >= 0xDC00 && w <= 0xDFFF) j = 2;
    } else if (encoding_ == TextEncoding::GBK) {
      // Sync from the nearest preceding byte < 0x30 (never a GBK/GB18030 trail
      // byte) or data_start_, then step forward to >= raw_start.
      uint32_t sync = raw_start;
      uint8_t probe[1024];
      bool synced = false;
      while (sync > data_start_ && !synced) {
        const size_t n = std::min<size_t>(sizeof(probe), sync - data_start_);
        sync -= static_cast<uint32_t>(n);
        if (!ReadExact(sync, probe, n)) return;
        for (size_t k = n; k > 0; --k) {
          if (probe[k - 1] < 0x30 || probe[k - 1] == 0x7F) {
            sync += static_cast<uint32_t>(k);
            synced = true;
            break;
          }
        }
      }
      // Re-read the main window after probing and walk from `sync` to
      // `raw_start`.
      const size_t total = back_pos_ - sync;
      if (!EnsureBuffer(total) || !ReadExact(sync, buf_, total)) return;
      size_t cur = 0;
      const size_t target = raw_start - sync;
      while (cur < target) cur += StepGbkChar(buf_, cur, total);
      win_start = sync + static_cast<uint32_t>(cur);
      slice = buf_ + cur;
      slice_len = total - cur;
      start_is_paragraph = (win_start == data_start_);
      break;
    }
    win_start = raw_start + static_cast<uint32_t>(j);
    slice = buf_ + j;
    slice_len = len - j;
    start_is_paragraph = (win_start == data_start_);
    break;
  }

  const size_t safe_len = SafeChunkEnd(slice, slice_len, encoding_);
  if (safe_len == 0) {
    if (win_start == data_start_) {
      back_pages_.push_back(data_start_);
      back_pos_ = data_start_;
      back_done_ = true;
    } else {
      back_pos_ = win_start;
    }
    return;
  }

  std::string decoded;
  const uint8_t* utf8_ptr = slice;
  size_t utf8_len = safe_len;
  if (encoding_ != TextEncoding::UTF8) {
    decoded = ToUtf8(slice, safe_len, encoding_);
    utf8_ptr = reinterpret_cast<const uint8_t*>(decoded.data());
    utf8_len = decoded.size();
  }

  std::vector<LineSpan> lines;
  MeasureLines(utf8_ptr, utf8_len, layout_, cache, start_is_paragraph, &lines);

  int max_lines = 1;
  if (layout_.line_height > 0 && layout_.text_h >= layout_.font_size) {
    max_lines = (layout_.text_h - layout_.font_size) / layout_.line_height + 1;
  }
  const int min_valid_line = start_is_paragraph ? 1 : 4;

  std::vector<size_t> desc_utf8;
  bool reached_data_start = false;
  int end_line = static_cast<int>(lines.size());
  while (end_line > 0) {
    int last_vis = end_line - 1;
    while (last_vis >= 0 && !lines[last_vis].has_visible) --last_vis;
    if (last_vis < 0) {
      if (win_start == data_start_) {
        desc_utf8.push_back(0);
        reached_data_start = true;
      }
      break;
    }

    int want_line = lines[last_vis].standalone_page
                        ? last_vis
                        : (last_vis - (max_lines - 1));
    bool after_standalone = false;
    if (!lines[last_vis].standalone_page) {
      for (int k = last_vis - 1; k >= std::max(0, want_line); --k) {
        if (lines[k].standalone_page) {
          want_line = k + 1;
          after_standalone = true;
          break;
        }
      }
    }
    while (want_line >= 0 && want_line < last_vis &&
           !lines[want_line].can_start_page) {
      ++want_line;
    }
    const int req_min_line = after_standalone ? 1 : min_valid_line;
    if (want_line < req_min_line) {
      if (win_start == data_start_) {
        desc_utf8.push_back(0);
        reached_data_start = true;
      }
      break;
    }

    const size_t prev_utf8 = lines[want_line].utf8_start;
    desc_utf8.push_back(prev_utf8);
    end_line = want_line;
    while (end_line > 0 && lines[end_line - 1].utf8_start >= prev_utf8) {
      --end_line;
    }
  }

  if (desc_utf8.empty()) {
    // A single page spanned the entire window (e.g. >16 KB of blank/space
    // bytes); grow the window for the next call.
    back_min_window_ =
        std::min<size_t>(cap * 2, static_cast<size_t>(back_pos_ - data_start_));
    return;
  }

  back_min_window_ = kWindowSize;
  std::vector<size_t> asc_utf8(desc_utf8.rbegin(), desc_utf8.rend());
  std::vector<size_t> asc_raw;
  MapUtf8OffsetsToRaw(slice, safe_len, encoding_, asc_utf8, &asc_raw);

  for (size_t idx = asc_raw.size(); idx > 0; --idx) {
    const uint32_t raw_off =
        win_start + static_cast<uint32_t>(asc_raw[idx - 1]);
    if (raw_off < back_pos_) {
      back_pages_.push_back(raw_off);
      back_pos_ = raw_off;
    }
  }
  if (reached_data_start || back_pos_ <= data_start_) {
    if (back_pages_.empty() || back_pages_.back() != data_start_) {
      back_pages_.push_back(data_start_);
    }
    back_pos_ = data_start_;
    back_done_ = true;
  }
}

void PageIndexer::StepForwardOnePage(const PageFontCache& cache) {
  if (fwd_done_ || read_error_) return;
  if (scan_pos_ >= file_size_) {
    fwd_done_ = true;
    return;
  }
  fwd_pages_.push_back(scan_pos_);

  LayoutCursor cursor;
  cursor.paragraph_start = next_paragraph_start_;
  bool page_filled = false;
  uint32_t offset = scan_pos_;
  while (!page_filled && offset < file_size_) {
    const uint8_t* chunk = nullptr;
    const size_t n = Window(offset, kChunkSize, &chunk);
    if (n == 0) break;

    const size_t end = SafeChunkEnd(chunk, n, encoding_);
    if (end == 0) {
      offset += static_cast<uint32_t>(n);
      continue;
    }
    const bool more = offset + static_cast<uint32_t>(n) < file_size_;
    if (encoding_ == TextEncoding::UTF8) {
      offset += static_cast<uint32_t>(MeasurePage(
          chunk, end, layout_, cache, &cursor, &page_filled, nullptr, more));
    } else {
      const std::string utf8 = ToUtf8(chunk, end, encoding_);
      const size_t used = MeasurePage(
          reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size(), layout_,
          cache, &cursor, &page_filled, nullptr, more);
      offset += static_cast<uint32_t>(
          RawBytesForUtf8Prefix(chunk, end, encoding_, used));
    }
  }

  if (read_error_ || offset == scan_pos_) {
    fwd_done_ = true;
    return;
  }
  scan_pos_ = offset;
  next_paragraph_start_ = cursor.paragraph_start;
  if (scan_pos_ >= file_size_) fwd_done_ = true;
}

void PageIndexer::ExportPages(std::vector<uint32_t>* pages) const {
  pages->clear();
  pages->reserve(back_pages_.size() + fwd_pages_.size());
  pages->insert(pages->end(), back_pages_.rbegin(), back_pages_.rend());
  pages->insert(pages->end(), fwd_pages_.begin(), fwd_pages_.end());
}

bool PageIndexer::Step(const PageFontCache& cache,
                       std::vector<uint32_t>* pages, uint32_t budget_ms) {
  if (!active() || done()) {
    if (done() && !read_error_) ExportPages(pages);
    return true;
  }
  const uint32_t t0 = millis();

  while (!back_done_ && !read_error_) {
    StepBackwardOneWindow(cache);
    if (millis() - t0 >= budget_ms) break;
  }

  while (back_done_ && !fwd_done_ && !read_error_) {
    StepForwardOnePage(cache);
    if (millis() - t0 >= budget_ms) break;
  }

  busy_ms_ += millis() - t0;
  if (read_error_) return true;
  if (done()) {
    ExportPages(pages);
    return true;
  }
  return false;
}

void PageIndexer::StepForPrevPage(const PageFontCache& cache,
                                  uint32_t current_offset) {
  if (!active() || read_error_ || current_offset <= data_start_) return;
  const uint32_t t0 = millis();
  if (current_offset <= anchor_) {
    while (!back_done_ && !read_error_ && back_pos_ >= current_offset) {
      StepBackwardOneWindow(cache);
    }
  } else {
    while (!fwd_done_ && !read_error_ && scan_pos_ < current_offset) {
      StepForwardOnePage(cache);
    }
  }
  busy_ms_ += millis() - t0;
}

bool PageIndexer::PrevPageOffset(uint32_t current_offset,
                                 uint32_t* prev_offset) const {
  if (current_offset <= data_start_) return false;
  if (current_offset <= anchor_) {
    auto it = std::upper_bound(back_pages_.begin(), back_pages_.end(),
                               current_offset, std::greater<uint32_t>());
    if (it == back_pages_.end()) return false;
    *prev_offset = *it;
    return true;
  }
  auto it =
      std::lower_bound(fwd_pages_.begin(), fwd_pages_.end(), current_offset);
  if (it == fwd_pages_.begin()) {
    if (!fwd_pages_.empty()) return false;
    *prev_offset = anchor_;
    return true;
  }
  --it;
  *prev_offset = *it;
  return true;
}

uint32_t PageIndexer::PageEndLimit(uint32_t current_offset) const {
  if (current_offset >= anchor_) return file_size_;
  auto it = std::lower_bound(back_pages_.begin(), back_pages_.end(),
                             current_offset, std::greater<uint32_t>());
  if (it != back_pages_.end() && *it == current_offset) {
    return (it == back_pages_.begin()) ? anchor_ : *(it - 1);
  }
  return anchor_;
}

int PageIndexer::CurrentPageIndex(uint32_t current_offset) const {
  if (!back_done_) return -1;
  const int back_count = static_cast<int>(back_pages_.size());
  if (current_offset < anchor_) {
    auto it = std::lower_bound(back_pages_.begin(), back_pages_.end(),
                               current_offset, std::greater<uint32_t>());
    if (it == back_pages_.end() || *it != current_offset) return -1;
    return (back_count - 1) -
           static_cast<int>(std::distance(back_pages_.begin(), it));
  }
  auto it =
      std::lower_bound(fwd_pages_.begin(), fwd_pages_.end(), current_offset);
  if (it == fwd_pages_.end() || *it != current_offset) return -1;
  return back_count + static_cast<int>(std::distance(fwd_pages_.begin(), it));
}

int PageIndexer::PageIndexContaining(uint32_t offset) const {
  if (!back_done_) return -1;
  const int back_count = static_cast<int>(back_pages_.size());
  if (offset < anchor_) {
    if (back_pages_.empty()) return 0;
    // `back_pages_` is descending: p_{k-1}, ..., p_0 (`data_start_`).
    // Find the first element `<= offset` in `back_pages_`.
    auto it = std::lower_bound(back_pages_.begin(), back_pages_.end(), offset,
                               std::greater<uint32_t>());
    if (it == back_pages_.end()) return 0;
    return (back_count - 1) -
           static_cast<int>(std::distance(back_pages_.begin(), it));
  }
  if (!fwd_done_ && offset >= scan_pos_) return -1;
  if (fwd_pages_.empty()) return std::max(0, back_count - 1);
  auto it = std::upper_bound(fwd_pages_.begin(), fwd_pages_.end(), offset);
  const int fwd_idx =
      (it == fwd_pages_.begin())
          ? 0
          : static_cast<int>(std::distance(fwd_pages_.begin(), it)) - 1;
  return back_count + fwd_idx;
}

int PageIndexer::IndexedPageCount() const {
  if (!back_done_) return 0;
  return static_cast<int>(back_pages_.size() + fwd_pages_.size());
}

uint32_t PageOffsetAtHelper(const std::vector<uint32_t>& back_pages,
                            const std::vector<uint32_t>& fwd_pages, int index) {
  const int back_count = static_cast<int>(back_pages.size());
  if (index < back_count) {
    return back_pages[(back_count - 1) - index];
  }
  return fwd_pages[index - back_count];
}

uint32_t PageIndexer::PageOffsetAt(int index) const {
  return PageOffsetAtHelper(back_pages_, fwd_pages_, index);
}

}  // namespace book
}  // namespace reader
