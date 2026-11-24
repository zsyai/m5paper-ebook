#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_BOOKSHELF_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_BOOKSHELF_H_

#include <cstdint>
#include <vector>

#include "book/bookshelf_sort.h"
#include "core/settings.h"

class Bookshelf {
 public:
  void init();
  const std::vector<BookInfo>& getBookList() const { return bookList; }
  void refresh(reader::core::BookshelfSort sort_mode =
                   reader::core::BookshelfSort::kRecentRead,
               uint32_t* io_read_seq = nullptr);
  // Re-orders the in-memory book list without re-reading the SD card.
  void resort(reader::core::BookshelfSort sort_mode);

 private:
  std::vector<BookInfo> bookList;
};

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_BOOKSHELF_H_
