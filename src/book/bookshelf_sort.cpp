#include "book/bookshelf_sort.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "book/gbk_unicode_table.h"
#include "book/utf8_utils.h"
#include "i18n/i18n.h"

namespace reader {
namespace book {
namespace {

bool IsLeadingIgnorable(uint32_t u) {
  return u == ' ' || u == '\t' || u == '\r' || u == '\n' || u == '(' ||
         u == '[' || u == '{' || u == '"' || u == '\'' || u == 0x3000 ||
         u == 0x300A || u == 0x3008 || u == 0x300C || u == 0x300E ||
         u == 0x3010 || u == 0x3014 || u == 0xFF08 || u == 0x201C ||
         u == 0x2018;
}

// Tier order for "最近阅读":
//   0 = `上次阅读` (`isLastRead` pinned at top)
//   1 = Sequenced opened books (`last_read_seq > 0`, including 100% completed
//       books, ordered strictly by `last_read_seq` descending)
//   2 = Legacy unsequenced in-progress books (`last_read_seq == 0`, < 100%)
//   3 = Legacy unsequenced completed books (`last_read_seq == 0`, >= 100%)
//   4 = Unread books (`!HasBookBeenOpened`)
int RecentReadBucket(const BookInfo& b) {
  if (b.isLastRead) return 0;
  if (b.last_read_seq > 0) return 1;
  if (!HasBookBeenOpened(b)) return 4;
  if (b.progress < 100) return 2;
  return 3;
}

// Tier order for "阅读进度":
//   0 = `上次阅读` (`isLastRead` pinned at top)
//   1 = `在读` (opened and < 100%, ordered by `progress` descending)
//   2 = `未读` (never opened, before completed books)
//   3 = `已读完` (>= 100%)
int ReadingProgressBucket(const BookInfo& b) {
  if (b.isLastRead) return 0;
  if (b.progress >= 100) return 3;
  if (HasBookBeenOpened(b)) return 1;
  return 2;
}

}  // namespace

bool HasBookBeenOpened(const BookInfo& book) {
  return book.has_record || book.isLastRead || book.last_read_seq > 0 ||
         book.pos > 0 || book.progress > 0;
}

const char* BookshelfSortName(core::BookshelfSort mode) {
  switch (mode) {
    case core::BookshelfSort::kRecentRead:
      return TR("最近阅读");
    case core::BookshelfSort::kTitle:
      return TR("书名");
    case core::BookshelfSort::kReadingProgress:
      return TR("阅读进度");
    case core::BookshelfSort::kRecentImport:
      return TR("最近导入");
  }
  return TR("最近阅读");
}

std::string BuildTitleSortKey(const std::string& title) {
  std::string key;
  key.reserve(title.size() * 2);

  const uint8_t* p = reinterpret_cast<const uint8_t*>(title.data());
  const uint8_t* end = p + title.size();
  bool skipped_leading = false;
  while (p < end) {
    int bytes = 0;
    const uint32_t u = Utf8Utils::getNextCharUnicode(p, end, bytes);
    if (bytes <= 0) {
      ++p;
      continue;
    }
    p += bytes;
    if (!skipped_leading) {
      if (IsLeadingIgnorable(u)) continue;
      skipped_leading = true;
    }
    if (u < 0x80) {
      key.push_back('\x01');
      key.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(u))));
    } else if (u <= 0xFFFF) {
      const uint16_t gbk = unicode_to_gbk_lookup(static_cast<uint16_t>(u));
      if (gbk != 0) {
        key.push_back('\x02');
        key.push_back(static_cast<char>((gbk >> 8) & 0xFF));
        key.push_back(static_cast<char>(gbk & 0xFF));
      } else {
        key.push_back('\x03');
        key.push_back(static_cast<char>((u >> 8) & 0xFF));
        key.push_back(static_cast<char>(u & 0xFF));
      }
    } else {
      key.push_back('\x04');
      key.push_back(static_cast<char>((u >> 16) & 0xFF));
      key.push_back(static_cast<char>((u >> 8) & 0xFF));
      key.push_back(static_cast<char>(u & 0xFF));
    }
  }
  return key;
}

void SortBooks(std::vector<BookInfo>* books, core::BookshelfSort mode) {
  if (books == nullptr || books->size() <= 1) return;
  const size_t n = books->size();

  std::vector<std::string> keys(n);
  for (size_t i = 0; i < n; ++i) {
    keys[i] = BuildTitleSortKey((*books)[i].title);
  }

  std::vector<size_t> order(n);
  std::iota(order.begin(), order.end(), 0);

  auto tie_title = [&](size_t i, size_t j) {
    if (keys[i] != keys[j]) return keys[i] < keys[j];
    return (*books)[i].filename < (*books)[j].filename;
  };

  std::stable_sort(order.begin(), order.end(), [&](size_t i, size_t j) {
    const BookInfo& a = (*books)[i];
    const BookInfo& b = (*books)[j];
    switch (mode) {
      case core::BookshelfSort::kRecentRead: {
        const int ba = RecentReadBucket(a);
        const int bb = RecentReadBucket(b);
        if (ba != bb) return ba < bb;
        if (ba == 1) {
          if (a.last_read_seq != b.last_read_seq) {
            return a.last_read_seq > b.last_read_seq;
          }
        } else if (ba == 2) {
          if (a.progress != b.progress) return a.progress > b.progress;
        }
        return tie_title(i, j);
      }
      case core::BookshelfSort::kTitle:
        return tie_title(i, j);
      case core::BookshelfSort::kReadingProgress: {
        const int ba = ReadingProgressBucket(a);
        const int bb = ReadingProgressBucket(b);
        if (ba != bb) return ba < bb;
        if (ba == 1) {
          if (a.progress != b.progress) return a.progress > b.progress;
          if (a.last_read_seq != b.last_read_seq) {
            return a.last_read_seq > b.last_read_seq;
          }
        } else if (ba == 3) {
          if (a.last_read_seq != b.last_read_seq) {
            return a.last_read_seq > b.last_read_seq;
          }
        }
        return tie_title(i, j);
      }
      case core::BookshelfSort::kRecentImport:
        if (a.import_time != b.import_time) {
          return a.import_time > b.import_time;
        }
        if (a.dir_order != b.dir_order) return a.dir_order > b.dir_order;
        return tie_title(i, j);
    }
    return tie_title(i, j);
  });

  std::vector<BookInfo> sorted;
  sorted.reserve(n);
  for (size_t idx : order) {
    sorted.push_back(std::move((*books)[idx]));
  }
  *books = std::move(sorted);
}

}  // namespace book
}  // namespace reader
