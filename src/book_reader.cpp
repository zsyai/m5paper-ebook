#include "book_reader.h"
#include "book/page_layout.h"
#include "core/perf_trace.h"
#include <M5Unified.h>
#include <algorithm>


BookReader::BookReader() : current_page_start_offset(0), next_page_start_offset(0) {}

BookReader::~BookReader() {
  if (file) {
    file.close();
  }
}


bool BookReader::open(const std::string& path) {
  PERF_SCOPE("BookReader::open");
  if (file) file.close();
  indexer_.Stop();
  page_index.clear();
  index_complete_ = false;
  current_page_index = 0;
  
  file = SD.open(path.c_str());
  if (!file) {
    Serial.println("Failed to open book file");
    return false;
  }
  
  current_book_path = path;
  file_size_ = file.size();
  current_page_start_offset = 0;
  next_page_start_offset = 0;
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
  }
  current_page_start_offset = next;
  paginate(cache, screen_w, screen_h);
  saveConfig();
  return true;
}

bool BookReader::prevPage(PageFontCache& cache, int screen_w, int screen_h) {
  if (!file || page_index.empty() || needsIndexForPrevPage()) return false;

  // Last indexed page that starts before the current one.
  auto it = std::lower_bound(page_index.begin(), page_index.end(),
                             current_page_start_offset);
  if (it == page_index.begin()) return false; // At beginning
  --it;
  current_page_index = std::distance(page_index.begin(), it);
  current_page_start_offset = *it;
  paginate(cache, screen_w, screen_h);
  saveConfig();
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

  std::string page;
  std::string prefetch;
  uint32_t offset = current_page_start_offset;
  bool page_filled = false;
  while (!page_filled && offset < file_size_) {
    if (!file.seek(offset)) break;
    const int read_bytes = file.read(buffer, kChunkSize);
    if (read_bytes <= 0) break;
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

  cache.build(page + prefetch, style_.font_path);
  current_page_text = page;
  next_page_start_offset = offset;
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
    if (index >= 0 && index < page_index.size()) {
        current_page_index = index;
        current_page_start_offset = page_index[current_page_index];
        paginate(cache, screen_w, screen_h);
        saveConfig();
        return true;
    }
    return false;
}

bool BookReader::jumpToPercentage(float percentage, PageFontCache& cache,
                                  int screen_w, int screen_h) {
    if (!index_complete_ || page_index.empty()) return false;
    int target_index = (page_index.size() - 1) * (percentage / 100.0f);
    if (target_index < 0) target_index = 0;
    if (target_index >= page_index.size()) target_index = page_index.size() - 1;
    return jumpToPage(target_index, cache, screen_w, screen_h);
}

bool BookReader::saveConfig() {
  PERF_SCOPE("BookReader::saveConfig");
  if (current_book_path.empty()) return false;
  
  size_t last_slash = current_book_path.find_last_of('/');
  std::string filename = (last_slash == std::string::npos) ? current_book_path : current_book_path.substr(last_slash + 1);
  
  std::string config_path = "/configs/" + filename + ".cfg";
  
  if (SD.exists(config_path.c_str())) {
    SD.remove(config_path.c_str());
  }
  
  File cfg_file = SD.open(config_path.c_str(), FILE_WRITE);
  if (!cfg_file) {
    Serial.println("Failed to open config file for writing");
    return false;
  }
  
  cfg_file.printf("pos=%u\n", current_page_start_offset);
  bool completed = index_complete_
      ? (!page_index.empty() && current_page_index + 1 == (int)page_index.size())
      : next_page_start_offset >= file_size_;
  cfg_file.printf("completed=%d\n", completed ? 1 : 0);
  for (uint32_t bm : bookmarks) {
    cfg_file.printf("bookmark=%u\n", bm);
  }
  
  cfg_file.close();
  return true;
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
  // While building, the number is known only once the indexer has reached
  // this page and it starts exactly on an indexed page start.
  auto it = std::lower_bound(page_index.begin(), page_index.end(),
                             current_page_start_offset);
  if (it == page_index.end() || *it != current_page_start_offset) return -1;
  return std::distance(page_index.begin(), it);
}

float BookReader::getProgress() const {
  if (index_complete_) {
    if (page_index.empty()) return 0.0f;
    if (current_page_index + 1 == (int)page_index.size()) return 100.0f;
    return (float)current_page_index / page_index.size() * 100.0f;
  }
  if (file_size_ <= file_data_start) return 0.0f;
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

void BookReader::prepareIndex(const PageFontCache& cache, int screen_w,
                              int screen_h) {
  PERF_SCOPE("BookReader::prepareIndex");
  if (!file) return;
  index_key_ = makeIndexKey(cache, screen_w, screen_h);
  std::string idx_path = reader::book::IndexPathForBook(current_book_path);
  if (reader::book::LoadPageIndex(idx_path, index_key_, &page_index)) {
    index_complete_ = true;
    locateCurrentPage();
    return;
  }
  startIndexBuild(cache, screen_w, screen_h);
}

void BookReader::startIndexBuild(const PageFontCache& cache, int screen_w,
                                 int screen_h) {
  page_index.clear();
  index_complete_ = false;
  index_key_ = makeIndexKey(cache, screen_w, screen_h);
  const reader::book::LayoutParams layout =
      layoutParams(cache, screen_w, screen_h);
  index_started_ms_ = millis();
  if (!indexer_.Start(current_book_path, current_encoding, file_data_start,
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
  if (indexer_.Step(cache, &page_index, budget_ms)) {
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
  Serial.printf("Index built with %d pages\n", page_index.size());
  indexer_.Stop();
  index_complete_ = true;
  reader::book::SavePageIndex(
      reader::book::IndexPathForBook(current_book_path), index_key_,
      page_index);
  // Keep the page on screen as is; later turns follow the index.
  locateCurrentPage();
}

bool BookReader::needsIndexForPrevPage() const {
  return !index_complete_ &&
         indexer_.scan_offset() < current_page_start_offset;
}

void BookReader::indexUntilCurrentPage(const PageFontCache& cache) {
  PERF_SCOPE("BookReader::indexUntilCurrentPage");
  while (hasPendingIndex() && needsIndexForPrevPage()) {
    stepIndex(cache, 200);
  }
}

void BookReader::indexUntilPageCount(const PageFontCache& cache, int count) {
  PERF_SCOPE("BookReader::indexUntilPageCount");
  while (hasPendingIndex() && (int)page_index.size() < count) {
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
