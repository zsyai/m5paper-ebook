#include "book/reading_records.h"

#include <dirent.h>
#include <esp32-hal-log.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <vector>

namespace reader {
namespace book {
namespace {

// No trailing '/': this FatFS build (FF_FS_RPATH = 0) rejects directory
// paths that end in a separator, so opendir() would fail.
constexpr char kConfigDir[] = "/sd/configs";
constexpr char kBookDir[] = "/sd/books";

bool EndsWith(const std::string& s, const char* suffix) {
  const std::string tail = suffix;
  return s.size() >= tail.size() &&
         s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

}  // namespace

int RemoveExpiredReadingRecords() {
  DIR* dir = opendir(kConfigDir);
  if (dir == nullptr) {
    log_e("records: cannot open %s (errno %d)", kConfigDir, errno);
    return 0;
  }
  // Collect first: deleting while iterating confuses FatFS readdir.
  std::vector<std::string> expired;
  while (struct dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (!EndsWith(name, ".cfg") && !EndsWith(name, ".idx")) continue;
    // Global settings, not per-book records.
    if (name == "global.cfg" || name == "settings.cfg") continue;
    const std::string book = name.substr(0, name.size() - 4);
    struct stat st;
    if (stat((std::string(kBookDir) + "/" + book).c_str(), &st) != 0) {
      expired.push_back(name);
    }
  }
  closedir(dir);

  int removed = 0;
  for (const std::string& name : expired) {
    const std::string path = std::string(kConfigDir) + "/" + name;
    struct stat st;
    // Skip directories that happen to match the pattern.
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if (unlink(path.c_str()) == 0) {
      log_i("records: removed %s", path.c_str());
      ++removed;
    }
  }
  log_i("records: removed %d of %u expired files", removed,
        static_cast<unsigned>(expired.size()));
  return removed;
}

}  // namespace book
}  // namespace reader
