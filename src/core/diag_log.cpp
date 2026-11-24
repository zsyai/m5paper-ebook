#include "core/diag_log.h"

#include <Preferences.h>
#include <esp_timer.h>
#include <sys/stat.h>

#include <cstdarg>
#include <cstdio>
#include <set>
#include <string>

#include "core/sd_path.h"

namespace reader {
namespace core {

namespace {

constexpr long kMaxLogBytes = 32 * 1024;
uint32_t g_boot = 0;
// Logs that already got this boot's header.
std::set<std::string>& HeaderWritten() {
  static std::set<std::string> written;
  return written;
}

}  // namespace

void InitDiagLog() {
  Preferences prefs;
  if (prefs.begin("reader", /*readOnly=*/false)) {
    g_boot = prefs.getUInt("boot", 0) + 1;
    prefs.putUInt("boot", g_boot);
    prefs.end();
  }
}

uint32_t BootNumber() { return g_boot; }

uint32_t UptimeMinutes() {
  return static_cast<uint32_t>(esp_timer_get_time() / 60000000ULL);
}

void DiagLog(const char* name, uint32_t minutes, const char* fmt, ...) {
  const std::string path = SdPath(std::string("/configs/") + name);
  struct stat st;
  if (stat(path.c_str(), &st) == 0 && st.st_size > kMaxLogBytes) {
    remove(path.c_str());
    HeaderWritten().erase(name);
  }
  FILE* f = fopen(path.c_str(), "a");
  if (!f) return;  // Best effort: a log must never block the caller.
  if (HeaderWritten().insert(name).second) {
    fprintf(f, "=== boot %u fw %s %s ===\n", g_boot, __DATE__, __TIME__);
  }
  fprintf(f, "boot=%u up=%um ", g_boot, minutes);
  va_list args;
  va_start(args, fmt);
  vfprintf(f, fmt, args);
  va_end(args);
  fputc('\n', f);
  fclose(f);
}

}  // namespace core
}  // namespace reader
