#include "book/page_indexer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>

namespace reader {
namespace book {
namespace {

// Raw bytes laid out per attempt; same as the old BookReader::buildIndex, so
// page starts stay identical to indexes built before this module existed.
constexpr size_t kChunkSize = 4096;
// Read window. Sequential SD reads run at ~1.1-1.2 MB/s in 32 KB requests.
constexpr size_t kWindowSize = 32 * 1024;

}  // namespace

PageIndexer::~PageIndexer() { Stop(); }

bool PageIndexer::Start(const std::string& book_path, TextEncoding encoding,
                        uint32_t data_start, uint32_t file_size,
                        const LayoutParams& layout) {
  Stop();
  if (!buf_) {
    buf_ = static_cast<uint8_t*>(
        heap_caps_malloc(kWindowSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf_) buf_ = static_cast<uint8_t*>(malloc(kWindowSize));
    if (!buf_) return false;
  }
  int fd = ::open(("/sd" + book_path).c_str(), O_RDONLY);
  if (fd < 0) return false;

  encoding_ = encoding;
  file_size_ = file_size;
  layout_ = layout;
  scan_pos_ = data_start;
  next_paragraph_start_ = true;  // The text starts a paragraph.
  buf_start_ = 0;
  buf_len_ = 0;
  file_pos_ = 0;
  done_ = false;
  read_error_ = false;
  busy_ms_ = 0;
  bytes_read_ = 0;
  fd_ = fd;  // Last: active() turns true only once the state is set up.
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
  }
}

size_t PageIndexer::Window(uint32_t offset, size_t want, const uint8_t** data) {
  if (offset < buf_start_ || offset > buf_start_ + buf_len_) {
    // Not contiguous with the window (only happens on the first call).
    if (::lseek(fd_, offset, SEEK_SET) < 0) {
      read_error_ = true;
      return 0;
    }
    file_pos_ = offset;
    buf_start_ = offset;
    buf_len_ = 0;
  }

  size_t have = buf_start_ + buf_len_ - offset;
  if (have < want && file_pos_ < file_size_) {
    // Slide the unread tail to the front and refill with forward reads.
    memmove(buf_, buf_ + (offset - buf_start_), have);
    buf_start_ = offset;
    buf_len_ = have;
    while (buf_len_ < kWindowSize && file_pos_ < file_size_) {
      ssize_t n = ::read(fd_, buf_ + buf_len_, kWindowSize - buf_len_);
      if (n <= 0) {
        if (n < 0) read_error_ = true;
        file_size_ = file_pos_;  // File is shorter than stat() said.
        break;
      }
      buf_len_ += n;
      file_pos_ += n;
      bytes_read_ += n;
    }
    have = buf_len_;
  }

  *data = buf_ + (offset - buf_start_);
  return have < want ? have : want;
}

bool PageIndexer::Step(const PageFontCache& cache,
                       std::vector<uint32_t>* pages, uint32_t budget_ms) {
  if (!active() || done_) return true;
  const uint32_t t0 = millis();

  while (!done_) {
    if (scan_pos_ >= file_size_) {
      done_ = true;
      break;
    }
    pages->push_back(scan_pos_);

    LayoutCursor cursor;
    cursor.paragraph_start = next_paragraph_start_;
    bool page_filled = false;
    uint32_t offset = scan_pos_;
    while (!page_filled && offset < file_size_) {
      const uint8_t* chunk = nullptr;
      size_t n = Window(offset, kChunkSize, &chunk);
      if (n == 0) break;

      size_t end = SafeChunkEnd(chunk, n, encoding_);
      if (end == 0) {
        offset += n;
        continue;
      }
      if (encoding_ == TextEncoding::UTF8) {
        offset += MeasurePage(chunk, end, layout_, cache, &cursor,
                              &page_filled);
      } else {
        std::string utf8 = ToUtf8(chunk, end, encoding_);
        size_t used = MeasurePage(reinterpret_cast<const uint8_t*>(utf8.data()),
                                  utf8.size(), layout_, cache, &cursor,
                                  &page_filled);
        offset += RawBytesForUtf8Prefix(chunk, end, encoding_, used);
      }
    }

    if (read_error_ || offset == scan_pos_) {
      done_ = true;  // No progress: end of text (or unreadable).
      break;
    }
    scan_pos_ = offset;
    next_paragraph_start_ = cursor.paragraph_start;
    if (millis() - t0 >= budget_ms) break;
  }

  busy_ms_ += millis() - t0;
  return done_;
}

}  // namespace book
}  // namespace reader
