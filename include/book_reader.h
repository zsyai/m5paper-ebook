#pragma once

#include <string>
#include <vector>
#include <SD.h>
#include "font_buffer.h"

enum class TextEncoding {
    UTF8,
    GBK,
    AUTO_DETECT
};

class BookReader {
 public:
  BookReader();
  ~BookReader();

  bool open(const std::string& path);
  bool nextPage(PageFontCache& cache, int screen_w, int screen_h);
  bool prevPage(PageFontCache& cache, int screen_w, int screen_h);
  bool readCurrentPage(PageFontCache& cache, int screen_w, int screen_h);
  std::string getPageText() const { return current_page_text; }
  std::string getBookPath() const { return current_book_path; }
  bool is_open() const { return file; }
  bool isHistoryEmpty() const { return page_offsets.empty(); }
  bool isIndexEmpty() const { return page_index.empty(); }
  uint32_t getCurrentOffset() const { return current_page_start_offset; }
  int getTotalPages() const { return page_index.size(); }
  int getCurrentPageIndex() const { return current_page_index; }
  bool jumpToPage(int index, PageFontCache& cache, int screen_w, int screen_h);
  bool jumpToPercentage(float percentage, PageFontCache& cache, int screen_w,
                        int screen_h);
  
  void seekTo(uint32_t offset);
  bool saveConfig();
  void setLineSpacingFactor(float factor) { line_spacing_factor = factor; }
  float getLineSpacingFactor() const { return line_spacing_factor; }
  std::string getFontPath() const { return font_path; }
  void setFontPath(const std::string& path) { font_path = path; }
  bool isLockScreenEnabled() const { return lock_screen_enabled; }
  void setLockScreenEnabled(bool enable) { lock_screen_enabled = enable; }
  std::string getLockScreenPath() const { return lock_screen_path; }
  void setLockScreenPath(const std::string& path) { lock_screen_path = path; }
  bool loadConfig(const std::string& book_path);
  bool saveGlobalConfig();
  bool loadGlobalConfig();
  bool buildIndex(PageFontCache& cache, int screen_w, int screen_h);
  bool saveIndex();
  bool loadIndex();
  void addBookmark();
  void removeBookmark(uint32_t offset);
  std::vector<uint32_t> getBookmarks() const { return bookmarks; }
  float getProgress() const {
    if (page_index.empty()) return 0.0f;
    if (current_page_index + 1 == page_index.size()) return 100.0f;
    return (float)current_page_index / page_index.size() * 100.0f;
  }

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
  float line_spacing_factor;
  TextEncoding current_encoding;
  std::string font_path;
  bool lock_screen_enabled;
  std::string lock_screen_path;
  
  void paginate(PageFontCache& cache, int screen_w, int screen_h);

};
