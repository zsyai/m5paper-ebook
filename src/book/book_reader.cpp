#include "book/book_reader.h"
#include "hal/storage.h"
#include "book/page_layout.h"
#include "core/perf_trace.h"
#include <M5Unified.h>
#include <algorithm>


BookReader::BookReader() : current_page_start_offset(0), next_page_start_offset(0) {}

void BookReader::close() {
  indexer_.Stop();
  if (file) file.close();
}

void BookReader::suspendContentFile() {
  if (file) file.close();
}

bool BookReader::resumeContentFile(const std::string& new_content_path) {
  reader::hal::CheckSdTaskInvariant("BookReader::resumeContentFile");
  if (file) file.close();
  content_path_ = new_content_path;
  file = SD.open(content_path_.c_str());
  if (!file) {
    reader::hal::ReportSdError();
    return false;
  }
  file_size_ = file.size();
  return true;
}

void BookReader::reset() {
  close();
  current_book_path.clear();
  content_path_.clear();
  content_complete_ = true;
  current_page_text.clear();
  prefetch_text_.clear();
  page_offsets.clear();
  page_index.clear();
  bookmarks.clear();
  current_page_index = 0;
  current_page_start_offset = 0;
  next_page_start_offset = 0;
  file_data_start = 0;
  file_size_ = 0;
  last_read_seq_ = 0;
  index_complete_ = false;
  mid_book_anchor_ = false;
  config_dirty_ = false;
  config_dirty_ms_ = 0;
}

BookReader::~BookReader() {
  if (file) {
    file.close();
  }
}


bool BookReader::open(const std::string& path,
                      const std::string& content_path) {
  PERF_SCOPE("BookReader::open");
  reader::hal::CheckSdTaskInvariant("BookReader::open");
  if (file && config_dirty_ && !reader::hal::SdErrorPending()) {
    saveConfig();
  }
  if (file) file.close();
  indexer_.Stop();
  page_index.clear();
  prefetch_text_.clear();
  index_complete_ = false;
  mid_book_anchor_ = false;
  config_dirty_ = false;
  content_complete_ = true;
  current_page_index = 0;

  const std::string resolved_content =
      content_path.empty() ? path : content_path;
  file = SD.open(resolved_content.c_str());
  if (!file) {
    // The bookshelf listed it a moment ago.
    reader::hal::ReportSdError();
    return false;
  }

  current_book_path = path;
  content_path_ = resolved_content;
  file_size_ = file.size();
  current_page_start_offset = 0;
  next_page_start_offset = 0;
  last_read_seq_ = 0;
  page_offsets.clear();
  bookmarks.clear();
  
  // Detect encoding
  uint8_t detect_buffer[1024];
  size_t detect_size = file.read(detect_buffer, sizeof(detect_buffer));
  current_encoding = reader::book::DetectEncoding(detect_buffer, detect_size);
  
  file_data_start = 0;
  if (current_encoding == TextEncoding::UTF8 && detect_size >= 3 && detect_buffer[0] == 0xEF && detect_buffer[1] == 0xBB && detect_buffer[2] == 0xBF) {
      file_data_start = 3;
  } else if (current_encoding == TextEncoding::UTF16LE && detect_size >= 2 && detect_buffer[0] == 0xFF && detect_buffer[1] == 0xFE) {
      file_data_start = 2;
  } else if (current_encoding == TextEncoding::UTF16BE && detect_size >= 2 && detect_buffer[0] == 0xFE && detect_buffer[1] == 0xFF) {
      file_data_start = 2;
  }
  
  current_page_start_offset = file_data_start;
  file.seek(current_page_start_offset); 
  
  // The page index is loaded or built by prepareIndex() once the body font
  // is known.
  return true;
}

bool BookReader::nextPage(PageFontCache& cache, int screen_w, int screen_h) {
  if (!file) return false;

  uint32_t next;
  if (index_complete_) {
    if (current_page_index + 1 >= (int)page_index.size()) return false;
    current_page_index++;
    next = page_index[current_page_index];
  } else {
    // Index still building: continue from where the shown page ended. The
    // indexer uses the same layout rules, so this lands on its page starts.
    if (next_page_start_offset <= current_page_start_offset ||
        next_page_start_offset >= file_size_) {
      return false;
    }
    next = next_page_start_offset;
    if (page_offsets.size() >= 256) {
      page_offsets.erase(page_offsets.begin());
    }
    page_offsets.push_back(current_page_start_offset);
  }
  current_page_start_offset = next;
  paginate(cache, screen_w, screen_h);
  markConfigDirty();
  return true;
}

bool BookReader::prevPage(PageFontCache& cache, int screen_w, int screen_h) {
  if (!file || current_page_start_offset <= file_data_start) return false;

  if (index_complete_) {
    if (page_index.empty()) return false;
    auto it = std::lower_bound(page_index.begin(), page_index.end(),
                               current_page_start_offset);
    if (it == page_index.begin()) return false;
    --it;
    current_page_index =
        static_cast<int>(std::distance(page_index.begin(), it));
    current_page_start_offset = *it;
  } else if (!indexer_.active()) {
    if (page_offsets.empty()) return false;
    current_page_start_offset = page_offsets.back();
    page_offsets.pop_back();
  } else {
    indexer_.StepForPrevPage(cache, current_page_start_offset);
    uint32_t prev = 0;
    if (!indexer_.PrevPageOffset(current_page_start_offset, &prev)) {
      return false;
    }
    current_page_start_offset = prev;
  }

  // Returning to the first page after a mid-book re-pagination triggers a fresh
  // forward pagination from `file_data_start` so page 1 is a full page.
  if (mid_book_anchor_ && current_page_start_offset <= file_data_start) {
    current_page_start_offset = file_data_start;
    prepareIndex(cache, screen_w, screen_h);
  }
  paginate(cache, screen_w, screen_h);
  markConfigDirty();
  return true;
}


bool BookReader::readCurrentPage(PageFontCache& cache, int screen_w, int screen_h) {
    if (!file) return false;
    paginate(cache, screen_w, screen_h);
    return !current_page_text.empty();
}


namespace {

// Raw bytes laid out per attempt, as in PageIndexer, so both split the text
// into the same chunks and find the same page ends.
constexpr size_t kChunkSize = 4096;

}  // namespace

reader::book::LayoutParams BookReader::layoutParams(const PageFontCache& cache,
                                                    int text_w,
                                                    int text_h) const {
  reader::book::LayoutParams layout;
  layout.text_w = text_w;
  layout.text_h = text_h;
  layout.font_size = cache.getFontSize();
  layout.line_height = style_.LineHeight(cache.getFontSize());
  layout.paragraph_indent = style_.paragraph_indent;
  return layout;
}

bool BookReader::startsParagraph(uint32_t offset) {
  if (offset <= file_data_start) return true;
  // GBK trail bytes are never 0x0A, so one byte is enough for 8-bit
  // encodings; UTF-16 needs the whole code unit.
  const bool utf16 = current_encoding == TextEncoding::UTF16LE ||
                     current_encoding == TextEncoding::UTF16BE;
  const uint32_t unit = utf16 ? 2 : 1;
  if (offset - file_data_start < unit) return true;
  uint8_t prev[2] = {0, 0};
  if (!file.seek(offset - unit) || file.read(prev, unit) != (int)unit) {
    return false;
  }
  if (!utf16) return prev[0] == '\n';
  return current_encoding == TextEncoding::UTF16LE
             ? (prev[0] == '\n' && prev[1] == 0)
             : (prev[0] == 0 && prev[1] == '\n');
}

void BookReader::paginate(PageFontCache& cache, int screen_w, int screen_h) {
  PERF_SCOPE("BookReader::paginate");
  reader::hal::CheckSdTaskInvariant("BookReader::paginate");
  current_page_text.clear();
  next_page_start_offset = current_page_start_offset;
  if (!file) return;
  // Opens the body font if needed; layout only reads its glyph index.
  cache.build(std::string(), style_.font_path);

  uint8_t* buffer = static_cast<uint8_t*>(malloc(kChunkSize));
  if (!buffer) return;

  const reader::book::LayoutParams layout =
      layoutParams(cache, screen_w, screen_h);
  reader::book::LayoutCursor cursor;
  if (layout.paragraph_indent) {
    cursor.paragraph_start = startsParagraph(current_page_start_offset);
  }

  uint32_t limit = file_size_;
  if (index_complete_) {
    if (current_page_index >= 0 &&
        current_page_index + 1 < static_cast<int>(page_index.size()) &&
        page_index[current_page_index] == current_page_start_offset) {
      limit = std::min(limit, page_index[current_page_index + 1]);
    }
  } else if (indexer_.active()) {
    limit = std::min(limit, indexer_.PageEndLimit(current_page_start_offset));
  }

  std::string page;
  std::string prefetch;
  uint32_t offset = current_page_start_offset;
  bool page_filled = false;
  while (!page_filled && offset < limit) {
    // Inside the file, so a failed seek or read means the card is gone.
    if (!file.seek(offset)) {
      reader::hal::ReportSdError();
      break;
    }
    const size_t want = std::min<size_t>(kChunkSize, limit - offset);
    const int read_bytes = file.read(buffer, want);
    if (read_bytes <= 0) {
      reader::hal::ReportSdError();
      break;
    }
    const size_t end = reader::book::SafeChunkEnd(buffer, read_bytes,
                                                  current_encoding);
    if (end == 0) {
      offset += read_bytes;
      continue;
    }
    const std::string utf8 =
        reader::book::ToUtf8(buffer, end, current_encoding);
    const size_t used = reader::book::MeasurePage(
        reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size(), layout,
        cache, &cursor, &page_filled, &page);
    offset += reader::book::RawBytesForUtf8Prefix(buffer, end,
                                                  current_encoding, used);
    // About one more page of text, so the next page turn finds most of
    // its glyphs cached. Bounded so large fonts stay within the cache.
    prefetch = utf8.substr(used, used);
  }
  free(buffer);

  if (!reader::book::ParseImagePageMarker(page, nullptr)) {
    cache.build(page, style_.font_path);
  } else {
    prefetch.clear();
  }
  current_page_text = page;
  prefetch_text_ = prefetch;
  next_page_start_offset = offset;
}

void BookReader::warmPrefetch(PageFontCache& cache) {
  if (prefetch_text_.empty() || !file) return;
  PERF_SCOPE("BookReader::warmPrefetch");
  std::string text;
  text.swap(prefetch_text_);
  cache.build(text, style_.font_path);
}


void BookReader::seekTo(uint32_t offset) {
  page_offsets.clear();
  
  if (page_index.empty()) {
    current_page_index = 0;
    current_page_start_offset = offset;
  } else {
    auto it = std::upper_bound(page_index.begin(), page_index.end(), offset);
    if (it == page_index.begin()) {
      current_page_index = 0;
    } else {
      current_page_index = std::distance(page_index.begin(), it) - 1;
    }
    current_page_start_offset = page_index[current_page_index];
  }
  next_page_start_offset = current_page_start_offset;
}

bool BookReader::jumpToPage(int index, PageFontCache& cache, int screen_w,
                            int screen_h) {
  const int total = getTotalPages();
  if (index < 0 || index >= total) return false;
  if (index_complete_) {
    current_page_index = index;
    current_page_start_offset = page_index[current_page_index];
  } else {
    current_page_start_offset = indexer_.PageOffsetAt(index);
  }
  if (mid_book_anchor_ && current_page_start_offset <= file_data_start) {
    current_page_start_offset = file_data_start;
    prepareIndex(cache, screen_w, screen_h);
  }
  paginate(cache, screen_w, screen_h);
  markConfigDirty();
  return true;
}

bool BookReader::jumpToPercentage(float percentage, PageFontCache& cache,
                                  int screen_w, int screen_h) {
  if (!index_complete_ || page_index.empty()) return false;
  const int target_index =
      reader::book::PageIndexForPercentage(page_index.size(), percentage);
  return jumpToPage(target_index, cache, screen_w, screen_h);
}

bool BookReader::jumpToOffset(uint32_t offset, PageFontCache& cache,
                              int screen_w, int screen_h,
                              bool start_index_if_incomplete) {
  if (!file) return false;
  uint32_t clamped = std::max(file_data_start, offset);
  if (file_size_ > file_data_start && clamped >= file_size_) {
    clamped = file_data_start;
  }
  if (index_complete_ && !page_index.empty()) {
    seekTo(clamped);
    if (mid_book_anchor_ && current_page_start_offset <= file_data_start) {
      current_page_start_offset = file_data_start;
      prepareIndex(cache, screen_w, screen_h);
    }
    paginate(cache, screen_w, screen_h);
    markConfigDirty();
    return true;
  }
  page_offsets.clear();
  current_page_start_offset = clamped;
  next_page_start_offset = clamped;
  if (start_index_if_incomplete && content_complete_) {
    prepareIndex(cache, screen_w, screen_h);
  }
  paginate(cache, screen_w, screen_h);
  markConfigDirty();
  return true;
}

uint32_t BookReader::peekSavedOffset(const std::string& book_path) {
  const size_t last_slash = book_path.find_last_of('/');
  const std::string filename = (last_slash == std::string::npos)
                                   ? book_path
                                   : book_path.substr(last_slash + 1);
  const std::string config_path = "/configs/" + filename + ".cfg";
  if (!SD.exists(config_path.c_str())) return 0;
  File cfg_file = SD.open(config_path.c_str(), FILE_READ);
  if (!cfg_file) return 0;
  uint32_t pos = 0;
  while (cfg_file.available()) {
    String line = cfg_file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("pos=")) {
      pos = static_cast<uint32_t>(line.substring(4).toInt());
      break;
    }
  }
  cfg_file.close();
  return pos;
}

bool BookReader::saveConfig() {
  PERF_SCOPE("BookReader::saveConfig");
  if (current_book_path.empty()) return false;
  config_dirty_ = false;
  
  size_t last_slash = current_book_path.find_last_of('/');
  std::string filename = (last_slash == std::string::npos) ? current_book_path : current_book_path.substr(last_slash + 1);
  
  std::string config_path = "/configs/" + filename + ".cfg";
  
  if (SD.exists(config_path.c_str())) {
    SD.remove(config_path.c_str());
  }
  
  File cfg_file = SD.open(config_path.c_str(), FILE_WRITE);
  reader::hal::CheckSdTaskInvariant("BookReader::saveConfig");
  if (!cfg_file) {
    reader::hal::ReportSdError();
    return false;
  }
  
  cfg_file.printf("pos=%u\n", current_page_start_offset);
  if (content_complete_ && file_size_ > 0) {
    cfg_file.printf("total=%u\n", file_size_);
  }
  const bool completed =
      content_complete_ &&
      (index_complete_
           ? (!page_index.empty() &&
              current_page_index + 1 == static_cast<int>(page_index.size()))
           : next_page_start_offset >= file_size_);
  if (content_complete_) {
    const int saved_progress = reader::book::RoundedBookshelfProgress(
        getProgress(), current_page_start_offset <= file_data_start, completed);
    cfg_file.printf("progress=%d\n", saved_progress);
  }
  cfg_file.printf("completed=%d\n", completed ? 1 : 0);
  if (last_read_seq_ > 0) {
    cfg_file.printf("last_read=%u\n", static_cast<unsigned>(last_read_seq_));
  }
  for (uint32_t bm : bookmarks) {
    cfg_file.printf("bookmark=%u\n", bm);
  }
  
  cfg_file.close();
  return true;
}

void BookReader::setLastReadSeq(uint32_t seq) {
  if (last_read_seq_ == seq) return;
  last_read_seq_ = seq;
  markConfigDirty();
}

void BookReader::markConfigDirty() {
  config_dirty_ = true;
  config_dirty_ms_ = millis();
}

bool BookReader::flushConfig() {
  if (!config_dirty_) return true;
  return saveConfig();
}

bool BookReader::flushConfigIfIdle(uint32_t now_ms, uint32_t idle_ms) {
  if (!config_dirty_ || now_ms - config_dirty_ms_ < idle_ms) return true;
  return saveConfig();
}

bool BookReader::loadConfig(const std::string& book_path) {
  PERF_SCOPE("BookReader::loadConfig");
  size_t last_slash = book_path.find_last_of('/');
  std::string filename = (last_slash == std::string::npos) ? book_path : book_path.substr(last_slash + 1);
  
  std::string config_path = "/configs/" + filename + ".cfg";
  
  if (!SD.exists(config_path.c_str())) {
    return false;
  }
  
  File cfg_file = SD.open(config_path.c_str(), FILE_READ);
  if (!cfg_file) {
    return false;
  }
  
  bookmarks.clear();
  
  while (cfg_file.available()) {
    String line = cfg_file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("pos=")) {
      uint32_t pos = line.substring(4).toInt();
      seekTo(pos);
    } else if (line.startsWith("last_read=")) {
      last_read_seq_ = static_cast<uint32_t>(
          std::strtoul(line.substring(10).c_str(), nullptr, 10));
    } else if (line.startsWith("bookmark=")) {
      uint32_t bm = line.substring(9).toInt();
      bookmarks.push_back(bm);
    }
  }
  
  cfg_file.close();
  return true;
}

// ---- Page index ---------------------------------------------------------

int BookReader::getCurrentPageIndex() const {
  if (index_complete_) return current_page_index;
  if (!indexer_.active()) return -1;
  return indexer_.CurrentPageIndex(current_page_start_offset);
}

int BookReader::getPageIndexForOffset(uint32_t offset) const {
  if (index_complete_) {
    if (page_index.empty()) return -1;
    auto it = std::upper_bound(page_index.begin(), page_index.end(), offset);
    return (it == page_index.begin())
               ? 0
               : static_cast<int>(std::distance(page_index.begin(), it)) - 1;
  }
  if (!indexer_.active()) return -1;
  return indexer_.PageIndexContaining(offset);
}

float BookReader::getProgress() const {
  if (index_complete_) {
    return reader::book::ProgressForPageIndex(current_page_index,
                                              page_index.size());
  }
  if (!content_complete_ || file_size_ <= file_data_start) return 0.0f;
  if (next_page_start_offset >= file_size_) return 100.0f;
  return (float)(current_page_start_offset - file_data_start) /
         (file_size_ - file_data_start) * 100.0f;
}

reader::book::IndexKey BookReader::makeIndexKey(const PageFontCache& cache,
                                                int screen_w,
                                                int screen_h) const {
  reader::book::IndexKey key;
  key.file_size = file_size_;
  key.data_start = file_data_start;
  key.font_hash = reader::book::HashString(cache.getLoadedFontPath());
  key.font_size = cache.getFontSize();
  key.line_height = style_.LineHeight(cache.getFontSize());
  key.text_w = screen_w;
  key.text_h = screen_h;
  key.encoding = static_cast<uint8_t>(current_encoding);
  key.margin = style_.margin;
  key.paragraph_indent = style_.paragraph_indent ? 1 : 0;
  // Only portrait vs. landscape changes page breaks (text_w / text_h carry
  // the size), so the upside-down directions reuse the same index.
  key.orientation = static_cast<uint8_t>(style_.orientation) & 1;
  return key;
}

void BookReader::locateCurrentPage() {
  auto it = std::upper_bound(page_index.begin(), page_index.end(),
                             current_page_start_offset);
  current_page_index =
      (it == page_index.begin()) ? 0 : std::distance(page_index.begin(), it) - 1;
}

void BookReader::prepareIndex(PageFontCache& cache, int screen_w,
                              int screen_h) {
  PERF_SCOPE("BookReader::prepareIndex");
  if (!file) return;
  indexer_.Stop();
  page_index.clear();
  index_complete_ = false;
  mid_book_anchor_ = false;

  // Open the body font first so makeIndexKey reads its resolved path and pixel
  // size before readCurrentPage() runs.
  cache.build(std::string(), style_.font_path);
  if (current_page_start_offset < file_data_start) {
    current_page_start_offset = file_data_start;
  } else if (current_page_start_offset >= file_size_ &&
             file_size_ > file_data_start) {
    current_page_start_offset = file_data_start;
  }

  index_key_ = makeIndexKey(cache, screen_w, screen_h);
  const std::string idx_path =
      reader::book::IndexPathForBook(current_book_path);
  bool loaded_mid_anchor = false;
  if (reader::book::LoadPageIndex(idx_path, index_key_, &page_index,
                                  &loaded_mid_anchor)) {
    const bool aligned = std::binary_search(
        page_index.begin(), page_index.end(), current_page_start_offset);
    const bool stale_start =
        (current_page_start_offset == file_data_start && loaded_mid_anchor);
    if (aligned && !stale_start) {
      index_complete_ = true;
      mid_book_anchor_ = loaded_mid_anchor;
      locateCurrentPage();
      return;
    }
    page_index.clear();
  }
  startIndexBuild(cache, screen_w, screen_h);
}

void BookReader::startIndexBuild(const PageFontCache& cache, int screen_w,
                                 int screen_h) {
  page_index.clear();
  index_complete_ = false;
  const uint32_t anchor =
      std::max(file_data_start,
               std::min(current_page_start_offset, file_size_));
  mid_book_anchor_ = (anchor > file_data_start);
  index_key_ = makeIndexKey(cache, screen_w, screen_h);
  const reader::book::LayoutParams layout =
      layoutParams(cache, screen_w, screen_h);
  index_started_ms_ = millis();
  if (!indexer_.Start(content_path_, current_encoding, file_data_start, anchor,
                      file_size_, layout)) {
    Serial.println("Failed to start page indexer");
  }
}

bool BookReader::stepIndex(const PageFontCache& cache, uint32_t budget_ms) {
  if (!hasPendingIndex()) return false;
  // The font or layout can change while the build runs (settings page);
  // page breaks from different inputs would be wrong, so start over.
  if (makeIndexKey(cache, index_key_.text_w, index_key_.text_h) !=
      index_key_) {
    startIndexBuild(cache, index_key_.text_w, index_key_.text_h);
    return false;
  }
  const bool done = indexer_.Step(cache, &page_index, budget_ms);
  if (indexer_.read_error()) {
    // The card failed mid-build: do not save or trust a truncated index.
    // The SD error handler remounts and reopens the book, which restarts
    // the build.
    indexer_.Stop();
    return false;
  }
  if (done) {
    onIndexComplete();
    return true;
  }
  return false;
}

void BookReader::onIndexComplete() {
  PERF_VALUE("index.pages", page_index.size());
  PERF_VALUE("index.busy_ms", indexer_.busy_ms());
  PERF_VALUE("index.wall_ms", millis() - index_started_ms_);
  PERF_VALUE("index.read_kb", indexer_.bytes_read() / 1024);
  Serial.printf("Index built with %d pages\n",
                static_cast<int>(page_index.size()));
  indexer_.Stop();
  index_complete_ = true;
  reader::book::SavePageIndex(
      reader::book::IndexPathForBook(current_book_path), index_key_,
      page_index, mid_book_anchor_);
  // Keep the page on screen as is; later turns follow the index.
  locateCurrentPage();
}

bool BookReader::needsIndexForPrevPage() const {
  if (index_complete_ || !indexer_.active() ||
      current_page_start_offset <= file_data_start) {
    return false;
  }
  // Backward steps read only a single 32 KB window (~30 ms); only show a
  // loading prompt if forward indexing lags far behind the current page.
  return current_page_start_offset > indexer_.anchor() &&
         current_page_start_offset > indexer_.scan_offset() + 64 * 1024;
}

void BookReader::indexUntilCurrentPage(const PageFontCache& cache) {
  PERF_SCOPE("BookReader::indexUntilCurrentPage");
  indexer_.StepForPrevPage(cache, current_page_start_offset);
}

void BookReader::indexUntilPageCount(const PageFontCache& cache, int count) {
  PERF_SCOPE("BookReader::indexUntilPageCount");
  while (hasPendingIndex() && getTotalPages() < count) {
    stepIndex(cache, 200);
  }
}

void BookReader::finishIndex(const PageFontCache& cache) {
  PERF_SCOPE("BookReader::finishIndex");
  while (hasPendingIndex()) {
    stepIndex(cache, 200);
  }
}



void BookReader::addBookmark() {
  for (uint32_t bm : bookmarks) {
    if (bm == current_page_start_offset) return;
  }
  bookmarks.push_back(current_page_start_offset);
  saveConfig();
}

void BookReader::removeBookmark(uint32_t offset) {
  auto it = std::find(bookmarks.begin(), bookmarks.end(), offset);
  if (it != bookmarks.end()) {
    bookmarks.erase(it);
    saveConfig();
  }
}
