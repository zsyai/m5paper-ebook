#pragma once

#include <string>
#include <vector>
#include <SD.h>
#include "book/page_index_file.h"
#include "book/page_indexer.h"
#include "book/reading_style.h"
#include "book/text_decode.h"
#include "fonts/font_buffer.h"

class BookReader {
 public:
  BookReader();
  ~BookReader();

  bool open(const std::string& path, const std::string& content_path = "");
  // Closes the file and stops the background index (e.g. before the SD card
  // is remounted). getBookPath() keeps the path, so the caller can reopen.
  void close();
  // Closes only the content `File` handle around an atomic SD append/rename on
  // the backing `.epc` file (FatFS `FF_FS_LOCK = 0` caches `FIL.obj.objsize`),
  // then `resumeContentFile()` reopens it and updates `file_size_`.
  void suspendContentFile();
  bool resumeContentFile(const std::string& new_content_path);
  void setContentComplete(bool complete) { content_complete_ = complete; }
  bool isContentComplete() const { return content_complete_; }
  // Closes the file, stops the background index, and clears all book state.
  void reset();
  bool nextPage(PageFontCache& cache, int screen_w, int screen_h);
  // Needs the index to reach the current page (see needsIndexForPrevPage()).
  bool prevPage(PageFontCache& cache, int screen_w, int screen_h);
  bool readCurrentPage(PageFontCache& cache, int screen_w, int screen_h);
  std::string getPageText() const { return current_page_text; }
  std::string getBookPath() const { return current_book_path; }
  const std::string& getContentPath() const { return content_path_; }
  bool is_open() const { return file; }
  bool isHistoryEmpty() const { return page_offsets.empty(); }
  uint32_t getCurrentOffset() const { return current_page_start_offset; }
  uint32_t getNextOffset() const { return next_page_start_offset; }
  uint32_t getDataStart() const { return file_data_start; }
  uint32_t getFileSize() const { return file_size_; }
  TextEncoding getEncoding() const { return current_encoding; }
  // Page count; only final once isIndexComplete().
  int getTotalPages() const {
    return index_complete_ ? static_cast<int>(page_index.size())
                           : indexer_.IndexedPageCount();
  }
  // 0-based page number, or -1 while the background index has not reached
  // the current page yet.
  int getCurrentPageIndex() const;
  // 0-based page number containing `offset`, or -1 if not yet indexed.
  int getPageIndexForOffset(uint32_t offset) const;
  // `index` must be < getTotalPages(); see indexUntilPageCount().
  bool jumpToPage(int index, PageFontCache& cache, int screen_w, int screen_h);
  // Needs a complete index; see finishIndex().
  bool jumpToPercentage(float percentage, PageFontCache& cache, int screen_w,
                        int screen_h);
  // Jumps directly to a chapter byte offset (using `page_index` when complete,
  // or re-anchoring bidirectional indexing immediately without blocking).
  bool jumpToOffset(uint32_t offset, PageFontCache& cache, int screen_w,
                    int screen_h, bool start_index_if_incomplete = true);
  static uint32_t peekSavedOffset(const std::string& book_path);
  
  void seekTo(uint32_t offset);
  bool saveConfig();
  bool flushConfig();
  bool flushConfigIfIdle(uint32_t now_ms, uint32_t idle_ms = 1500);
  // Pre-loads the next page's glyphs into `cache` during idle time so page
  // turns do not pay next-page glyph misses on the critical path.
  void warmPrefetch(PageFontCache& cache);
  // Layout and font of the body text. Pages already read keep their layout;
  // the next pagination and prepareIndex() use the new style.
  void setStyle(const reader::book::ReadingStyle& style) { style_ = style; }
  const reader::book::ReadingStyle& style() const { return style_; }
  // Per-book state (position, bookmarks, last_read) from /configs/<book>.cfg.
  bool loadConfig(const std::string& book_path);
  uint32_t lastReadSeq() const { return last_read_seq_; }
  void setLastReadSeq(uint32_t seq);

  // Page index. prepareIndex() loads a matching .idx aligned with the current
  // reading offset, or starts building one bidirectionally from the current
  // offset in the background; stepIndex() advances that build and saves the
  // .idx when it finishes. Call before readCurrentPage() so backward page
  // bounds are ready when laying out the current page.
  void prepareIndex(PageFontCache& cache, int screen_w, int screen_h);
  bool isIndexComplete() const { return index_complete_; }
  bool hasPendingIndex() const { return !index_complete_ && indexer_.active(); }
  // Returns true on the call that completes the index.
  bool stepIndex(const PageFontCache& cache, uint32_t budget_ms);
  bool needsIndexForPrevPage() const;
  void indexUntilCurrentPage(const PageFontCache& cache);
  void indexUntilPageCount(const PageFontCache& cache, int count);
  void finishIndex(const PageFontCache& cache);

  void addBookmark();
  void removeBookmark(uint32_t offset);
  std::vector<uint32_t> getBookmarks() const { return bookmarks; }
  // Percent read: by page once the index is complete, by byte offset before.
  float getProgress() const;

 private:
  File file;
  std::string current_page_text;
  std::vector<uint32_t> page_offsets; // Stack of start offsets of visited pages
  std::vector<uint32_t> page_index;   // All page start offsets
  int current_page_index;             // Current page number
  uint32_t current_page_start_offset;
  uint32_t next_page_start_offset;
  
  std::string current_book_path;
  std::string content_path_;
  bool content_complete_ = true;
  std::vector<uint32_t> bookmarks;
  TextEncoding current_encoding;
  reader::book::ReadingStyle style_;
  uint32_t file_data_start = 0;
  uint32_t file_size_ = 0;
  uint32_t last_read_seq_ = 0;

  reader::book::PageIndexer indexer_;
  reader::book::IndexKey index_key_;
  bool index_complete_ = false;
  bool mid_book_anchor_ = false;
  uint32_t index_started_ms_ = 0;
  std::string prefetch_text_;
  bool config_dirty_ = false;
  uint32_t config_dirty_ms_ = 0;

  void markConfigDirty();

  // Lays out the page at current_page_start_offset into current_page_text
  // (with the same rules and chunking as the indexer), sets
  // next_page_start_offset and caches the page's glyphs.
  void paginate(PageFontCache& cache, int screen_w, int screen_h);
  reader::book::LayoutParams layoutParams(const PageFontCache& cache,
                                          int text_w, int text_h) const;
  // True if `offset` is the start of the text or follows a '\n'.
  bool startsParagraph(uint32_t offset);
  reader::book::IndexKey makeIndexKey(const PageFontCache& cache, int screen_w,
                                      int screen_h) const;
  void startIndexBuild(const PageFontCache& cache, int screen_w, int screen_h);
  void onIndexComplete();
  void locateCurrentPage();
};
