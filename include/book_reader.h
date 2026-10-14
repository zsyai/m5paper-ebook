#pragma once

#include <string>
#include <vector>
#include <SD.h>
#include "book/page_index_file.h"
#include "book/page_indexer.h"
#include "book/reading_style.h"
#include "book/text_decode.h"
#include "font_buffer.h"

class BookReader {
 public:
  BookReader();
  ~BookReader();

  bool open(const std::string& path);
  bool nextPage(PageFontCache& cache, int screen_w, int screen_h);
  // Needs the index to reach the current page (see needsIndexForPrevPage()).
  bool prevPage(PageFontCache& cache, int screen_w, int screen_h);
  bool readCurrentPage(PageFontCache& cache, int screen_w, int screen_h);
  std::string getPageText() const { return current_page_text; }
  std::string getBookPath() const { return current_book_path; }
  bool is_open() const { return file; }
  bool isHistoryEmpty() const { return page_offsets.empty(); }
  uint32_t getCurrentOffset() const { return current_page_start_offset; }
  // Page count; only final once isIndexComplete().
  int getTotalPages() const { return page_index.size(); }
  // 0-based page number, or -1 while the background index has not reached
  // the current page yet.
  int getCurrentPageIndex() const;
  // `index` must be < getTotalPages(); see indexUntilPageCount().
  bool jumpToPage(int index, PageFontCache& cache, int screen_w, int screen_h);
  // Needs a complete index; see finishIndex().
  bool jumpToPercentage(float percentage, PageFontCache& cache, int screen_w,
                        int screen_h);
  
  void seekTo(uint32_t offset);
  bool saveConfig();
  // Layout and font of the body text. Pages already read keep their layout;
  // the next pagination and prepareIndex() use the new style.
  void setStyle(const reader::book::ReadingStyle& style) { style_ = style; }
  const reader::book::ReadingStyle& style() const { return style_; }
  // Per-book state (position, bookmarks) from /configs/<book>.cfg.
  bool loadConfig(const std::string& book_path);

  // Page index. prepareIndex() loads a matching .idx, or starts building one
  // in the background; stepIndex() advances that build and saves the .idx
  // when it finishes. Call after readCurrentPage() so the body font is loaded.
  void prepareIndex(const PageFontCache& cache, int screen_w, int screen_h);
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
  std::vector<uint32_t> bookmarks;
  TextEncoding current_encoding;
  reader::book::ReadingStyle style_;
  uint32_t file_data_start = 0;
  uint32_t file_size_ = 0;

  reader::book::PageIndexer indexer_;
  reader::book::IndexKey index_key_;
  bool index_complete_ = false;
  uint32_t index_started_ms_ = 0;
  
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
