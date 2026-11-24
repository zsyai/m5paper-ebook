#include "book/reading_records.h"

#include <dirent.h>
#include <esp32-hal-log.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/sd_path.h"
#include "hal/storage.h"

namespace reader {
namespace book {
namespace {

// No trailing '/': this FatFS build (FF_FS_RPATH = 0) rejects directory
// paths that end in a separator, so opendir() would fail.

bool EndsWith(const std::string& s, const char* suffix) {
  const std::string tail = suffix;
  return s.size() >= tail.size() &&
         s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

std::string ReadFirstLineTrimmed(const std::string& path) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return "";
  char buf[320];
  std::string line;
  if (fgets(buf, sizeof(buf), f)) {
    const char* begin = buf;
    while (*begin == ' ' || *begin == '\t') ++begin;
    const char* end = begin + strlen(begin);
    while (end > begin && (end[-1] == '\n' || end[-1] == '\r' ||
                           end[-1] == ' ' || end[-1] == '\t')) {
      --end;
    }
    line.assign(begin, end);
  }
  fclose(f);
  return line;
}

void ClearLastReadIfMatches(const std::string& filename) {
  const std::string global_cfg = core::SdPath("/configs/global.cfg");
  const std::string last = ReadFirstLineTrimmed(global_cfg);
  if (last == "/books/" + filename || last == filename) {
    unlink(global_cfg.c_str());
    unlink(core::SdPath("/configs/boot_to_reader.flag").c_str());
  }
}

bool ValidBookFilename(const std::string& filename) {
  return !filename.empty() && filename.find('/') == std::string::npos;
}

struct ExpiredRecordFile {
  std::string path;
  uint64_t bytes;
};

std::vector<ExpiredRecordFile> CollectExpiredRecordFiles() {
  std::vector<ExpiredRecordFile> expired;

  std::unordered_set<std::string> active_books;
  DIR* books_dir = opendir(core::SdPath("/books").c_str());
  if (books_dir != nullptr) {
    while (struct dirent* entry = readdir(books_dir)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") continue;
      active_books.insert(name);
    }
    closedir(books_dir);
  } else if (errno != ENOENT) {
    log_e("records: cannot open /books (errno %d)", errno);
    return expired;
  }

  const std::string configs_dir_path = core::SdPath("/configs");
  DIR* dir = opendir(configs_dir_path.c_str());
  if (dir == nullptr) {
    if (errno != ENOENT) {
      log_e("records: cannot open /configs (errno %d)", errno);
    }
    return expired;
  }
  // Collect first: deleting while iterating confuses FatFS readdir.
  std::vector<std::string> candidates;
  while (struct dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    // Global settings, not per-book records.
    if (name == "global.cfg" || name == "settings.cfg") continue;
    size_t suffix_len = 0;
    if (EndsWith(name, ".epc.tmp") || EndsWith(name, ".toc.tmp")) {
      suffix_len = 8;
    } else if (EndsWith(name, ".cfg") || EndsWith(name, ".idx") ||
               EndsWith(name, ".epc") || EndsWith(name, ".toc")) {
      suffix_len = 4;
    } else {
      continue;
    }
    const std::string book = name.substr(0, name.size() - suffix_len);
    if (active_books.find(book) == active_books.end()) {
      candidates.push_back(name);
    }
  }
  closedir(dir);

  expired.reserve(candidates.size());
  for (const std::string& name : candidates) {
    const std::string path = configs_dir_path + "/" + name;
    struct stat st;
    // Skip directories that happen to match the pattern.
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
    expired.push_back({path, static_cast<uint64_t>(st.st_size)});
  }
  return expired;
}

}  // namespace

ExpiredRecordsUsage ScanExpiredReadingRecords() {
  ExpiredRecordsUsage usage;
  const std::vector<ExpiredRecordFile> expired = CollectExpiredRecordFiles();
  usage.files = static_cast<int>(expired.size());
  for (const ExpiredRecordFile& file : expired) {
    usage.bytes += file.bytes;
  }
  log_i("records: %d expired files (%u KB)", usage.files,
        static_cast<unsigned>(usage.bytes / 1024));
  return usage;
}

int RemoveExpiredReadingRecords() {
  const std::vector<ExpiredRecordFile> expired = CollectExpiredRecordFiles();
  int removed = 0;
  for (const ExpiredRecordFile& file : expired) {
    if (unlink(file.path.c_str()) == 0) {
      log_i("records: removed %s", file.path.c_str());
      ++removed;
    }
  }
  log_i("records: removed %d of %u expired files", removed,
        static_cast<unsigned>(expired.size()));
  return removed;
}

bool ClearBookReadingRecord(const std::string& filename) {
  if (!ValidBookFilename(filename)) return false;
  const std::string cfg = core::SdPath("/configs/" + filename + ".cfg");
  if (unlink(cfg.c_str()) != 0 && errno != ENOENT) {
    hal::ReportSdError();
    return false;
  }
  ClearLastReadIfMatches(filename);
  return true;
}

bool DeleteBookFiles(const std::string& filename) {
  if (!ValidBookFilename(filename)) return false;
  const std::string book_path = core::SdPath("/books/" + filename);
  if (unlink(book_path.c_str()) != 0) {
    if (errno != ENOENT) hal::ReportSdError();
    return false;
  }
  static const char* const kSidecars[] = {".cfg", ".idx",     ".epc",
                                          ".toc", ".epc.tmp", ".toc.tmp"};
  for (const char* ext : kSidecars) {
    const std::string sidecar = core::SdPath("/configs/" + filename + ext);
    if (unlink(sidecar.c_str()) != 0 && errno != ENOENT) hal::ReportSdError();
  }
  ClearLastReadIfMatches(filename);
  return true;
}

}  // namespace book
}  // namespace reader
