#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_BOOKSHELF_SORT_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_BOOKSHELF_SORT_H_

// Portable bookshelf entry metadata and sorting comparators (recent open
// sequence, GB2312 Pinyin / ASCII title collation, reading progress, and
// recent import order).

#include <cstdint>
#include <string>
#include <vector>

#include "core/settings.h"

struct BookInfo {
  std::string filename;
  std::string title;
  uint32_t size = 0;
  uint32_t pos = 0;
  int progress = 0;
  uint32_t last_read_seq = 0;
  uint32_t import_time = 0;
  uint32_t dir_order = 0;
  bool has_record = false;
  bool isLastRead = false;
};

namespace reader {
namespace book {

// True when `book` has any saved reading state (even at 0% on page 1).
bool HasBookBeenOpened(const BookInfo& book);

// Localized UI label for `mode` ("最近阅读", "书名", "阅读进度", "最近导入").
const char* BookshelfSortName(core::BookshelfSort mode);

// Builds a byte-comparable collation key for `title`: strips leading book/quote
// punctuation, folds ASCII case, and maps BMP Chinese characters through the
// built-in GB2312/GBK table so Level-1 characters sort in Pinyin A-Z order.
std::string BuildTitleSortKey(const std::string& title);

// Sorts `books` in place according to `mode`.
void SortBooks(std::vector<BookInfo>* books, core::BookshelfSort mode);

}  // namespace book
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_BOOKSHELF_SORT_H_
