#include "book/bookshelf.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "book/book_toc.h"
#include "book/epub_book.h"
#include "core/perf_trace.h"
#include "core/sd_path.h"
#include "hal/storage.h"

namespace {

std::string TrimLine(const char* s) {
  const char* begin = s;
  while (*begin == ' ' || *begin == '\t') ++begin;
  const char* end = begin + std::strlen(begin);
  while (end > begin && (end[-1] == '\n' || end[-1] == '\r' ||
                         end[-1] == ' ' || end[-1] == '\t')) {
    --end;
  }
  return std::string(begin, end);
}

bool EndsWithAsciiCaseInsensitive(const std::string& s, const char* suffix) {
  const size_t slen = std::strlen(suffix);
  if (s.size() < slen) return false;
  const size_t off = s.size() - slen;
  for (size_t i = 0; i < slen; ++i) {
    char a = s[off + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

bool IsSupportedBookName(const std::string& filename) {
  if (filename.empty() || filename[0] == '.') return false;
  return EndsWithAsciiCaseInsensitive(filename, ".txt") ||
         EndsWithAsciiCaseInsensitive(filename, ".epub");
}

}  // namespace

void Bookshelf::init() { refresh(); }

void Bookshelf::refresh(reader::core::BookshelfSort sort_mode,
                        uint32_t* io_read_seq) {
  PERF_SCOPE("Bookshelf::refresh");
  bookList.clear();
  uint32_t max_seq = 0;

  std::string last_read_filename;
  if (FILE* gcfg =
          std::fopen(reader::core::SdPath("/configs/global.cfg").c_str(), "r")) {
    char line[320];
    if (std::fgets(line, sizeof(line), gcfg)) {
      const std::string path = TrimLine(line);
      const size_t slash = path.find_last_of('/');
      last_read_filename =
          (slash == std::string::npos) ? path : path.substr(slash + 1);
    }
    std::fclose(gcfg);
  }

  // Note: FatFS (`FF_FS_RPATH = 0`) rejects trailing slashes in `opendir`.
  const std::string books_dir = reader::core::SdPath("/books");
  DIR* dir = ::opendir(books_dir.c_str());
  if (dir == nullptr) {
    reader::hal::ReportSdError();
    return;
  }

  // Collect entries and close the directory handle first to keep open FD count
  // minimal before reading `.cfg` and `.toc`/`.epub` metadata.
  uint32_t dir_order = 0;
  while (struct dirent* ent = ::readdir(dir)) {
    const std::string filename = ent->d_name;
    if (!IsSupportedBookName(filename)) continue;
    struct stat st;
    const std::string full_path = books_dir + "/" + filename;
    if (::stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;

    BookInfo info;
    info.filename = filename;
    info.title = reader::book::StripBookExtension(filename);
    info.size = static_cast<uint32_t>(st.st_size);
    info.import_time =
        (st.st_mtime > 0) ? static_cast<uint32_t>(st.st_mtime) : 0u;
    info.dir_order = dir_order++;
    info.isLastRead = (filename == last_read_filename);
    bookList.push_back(std::move(info));
  }
  ::closedir(dir);

  for (BookInfo& info : bookList) {
    if (reader::book::IsEpubPath(info.filename)) {
      std::string epub_title;
      if (reader::book::ResolveEpubDisplayTitle("/books/" + info.filename,
                                                &epub_title) &&
          !epub_title.empty()) {
        info.title = epub_title;
      }
    }

    const std::string cfg_path =
        reader::core::SdPath("/configs/" + info.filename + ".cfg");
    bool completed = false;
    uint32_t total_bytes = 0;
    int saved_progress = -1;
    if (FILE* cfg_file = std::fopen(cfg_path.c_str(), "r")) {
      info.has_record = true;
      char raw_line[160];
      while (std::fgets(raw_line, sizeof(raw_line), cfg_file)) {
        const std::string line = TrimLine(raw_line);
        if (line.rfind("pos=", 0) == 0) {
          info.pos =
              static_cast<uint32_t>(std::strtoul(line.c_str() + 4, nullptr, 10));
        } else if (line.rfind("total=", 0) == 0) {
          total_bytes =
              static_cast<uint32_t>(std::strtoul(line.c_str() + 6, nullptr, 10));
        } else if (line.rfind("progress=", 0) == 0) {
          saved_progress = std::atoi(line.c_str() + 9);
        } else if (line.rfind("last_read=", 0) == 0) {
          info.last_read_seq = static_cast<uint32_t>(
              std::strtoul(line.c_str() + 10, nullptr, 10));
          max_seq = std::max(max_seq, info.last_read_seq);
        } else if (line.rfind("completed=", 0) == 0) {
          completed = std::atoi(line.c_str() + 10) == 1;
        }
      }
      std::fclose(cfg_file);
    }

    const uint32_t denom = (total_bytes > 0) ? total_bytes : info.size;
    if (completed) {
      info.progress = 100;
    } else if (saved_progress >= 0) {
      info.progress = std::min(99, saved_progress);
    } else if (denom > 0 && info.pos > 0) {
      const uint64_t pct = (static_cast<uint64_t>(info.pos) * 100ull) / denom;
      info.progress = (pct >= 100ull) ? 99 : static_cast<int>(pct);
    } else {
      info.progress = 0;
    }
  }

  if (io_read_seq != nullptr) {
    *io_read_seq = std::max(*io_read_seq, max_seq);
    // Migrate pre-`last_read=` `global.cfg` state once so the previously active
    // book retains its sequence number when another book is opened later.
    for (BookInfo& info : bookList) {
      if (info.isLastRead && info.has_record && info.last_read_seq == 0) {
        *io_read_seq += 1;
        info.last_read_seq = *io_read_seq;
        const std::string cfg_path =
            reader::core::SdPath("/configs/" + info.filename + ".cfg");
        if (FILE* f = std::fopen(cfg_path.c_str(), "a")) {
          std::fprintf(f, "last_read=%u\n",
                       static_cast<unsigned>(info.last_read_seq));
          std::fclose(f);
        }
      }
    }
  }

  reader::book::SortBooks(&bookList, sort_mode);
}

void Bookshelf::resort(reader::core::BookshelfSort sort_mode) {
  reader::book::SortBooks(&bookList, sort_mode);
}
