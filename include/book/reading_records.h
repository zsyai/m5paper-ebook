#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_READING_RECORDS_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_READING_RECORDS_H_

// Per-book files in /configs/: "<book file>.cfg" (position, bookmarks),
// "<book file>.idx" (page index), and "<book file>.epc"/".toc" (EPUB/TOC
// caches), named after a file in /books/.

#include <cstdint>
#include <string>

namespace reader {
namespace book {

struct ExpiredRecordsUsage {
  int files = 0;
  uint64_t bytes = 0;
};

// Scans /configs/ for orphaned per-book sidecar files whose book is no longer
// in /books/.
ExpiredRecordsUsage ScanExpiredReadingRecords();

// Deletes the orphaned sidecar files of books no longer in /books/. Returns the
// number of files deleted.
int RemoveExpiredReadingRecords();

// Clears the reading progress (.cfg) of `filename` (under /books/), keeping
// its page index (.idx). Also clears global.cfg and boot_to_reader.flag if
// they point to this book.
bool ClearBookReadingRecord(const std::string& filename);

// Deletes /books/`filename` along with its .cfg and .idx files, and clears
// global.cfg and boot_to_reader.flag if they point to this book.
bool DeleteBookFiles(const std::string& filename);

}  // namespace book
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_READING_RECORDS_H_
