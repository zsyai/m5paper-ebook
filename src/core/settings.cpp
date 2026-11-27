#include "core/settings.h"
#include "hal/storage.h"
#include "core/sd_path.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/perf_trace.h"

namespace reader {
namespace core {
namespace {

// The SD card is mounted at /sd for POSIX access (see src/main.cpp).
std::string SettingsPath() { return SdPath("/configs/settings.cfg"); }

int Clamp(int value, int lo, int hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

// Strips leading and trailing whitespace (including the '\n' from fgets).
std::string Trim(const char* s) {
  const char* begin = s;
  while (*begin == ' ' || *begin == '\t') ++begin;
  const char* end = begin + strlen(begin);
  while (end > begin && (end[-1] == '\n' || end[-1] == '\r' ||
                         end[-1] == ' ' || end[-1] == '\t')) {
    --end;
  }
  return std::string(begin, end);
}

bool ParseBool(const std::string& v) { return atoi(v.c_str()) == 1; }

TouchAction ParseAction(int v) {
  return static_cast<TouchAction>(Clamp(v, 0, kTouchActionCount - 1));
}

// Reads up to `n` comma-separated action codes into `out`; missing or
// extra entries keep the defaults.
void ParseActions(const std::string& v, TouchAction* out, int n) {
  const char* s = v.c_str();
  for (int i = 0; i < n && *s; ++i) {
    char* end = nullptr;
    const long code = strtol(s, &end, 10);
    if (end == s) break;
    out[i] = ParseAction(static_cast<int>(code));
    s = (*end == ',') ? end + 1 : end;
  }
}

void PrintActions(FILE* f, const char* key, const TouchAction* a, int n) {
  fprintf(f, "%s=", key);
  for (int i = 0; i < n; ++i) {
    fprintf(f, i ? ",%d" : "%d", static_cast<int>(a[i]));
  }
  fprintf(f, "\n");
}

const Settings kDefaults;

}  // namespace

int ClampBodyFontSize(int size) {
  size = Clamp(size, kMinBodyFontSize, kMaxBodyFontSize);
  return size - (size - kMinBodyFontSize) % kBodyFontSizeStep;
}

float ClampLineSpacing(float spacing) {
  if (!(spacing >= kMinLineSpacing)) return kMinLineSpacing;  // Also NaN.
  if (spacing > kMaxLineSpacing) return kMaxLineSpacing;
  const int tenths = static_cast<int>(std::lround(spacing * 10.0f));
  return tenths / 10.0f;
}

int ClampMargin(int margin) { return Clamp(margin, kMinMargin, kMaxMargin); }

int ClampFullRefreshPages(int pages) {
  return Clamp(pages, kMinFullRefreshPages, kMaxFullRefreshPages);
}

int ClampAutoTurnSeconds(int seconds) {
  return Clamp(seconds, kMinAutoTurnSeconds, kMaxAutoTurnSeconds);
}

TouchAction Settings::TapAction(int x, int y, int w, int h) const {
  if (w <= 0 || h <= 0) return TouchAction::kNone;
  const int col = Clamp(x * 3 / w, 0, 2);
  if (tap_layout == TapLayout::kColumns) return tap_columns[col];
  const int row = Clamp(y * 3 / h, 0, 2);
  return tap_grid[row * 3 + col];
}

void Settings::ResetTapZones() {
  if (tap_layout == TapLayout::kColumns) {
    std::copy(kDefaults.tap_columns, kDefaults.tap_columns + 3, tap_columns);
  } else {
    std::copy(kDefaults.tap_grid, kDefaults.tap_grid + 9, tap_grid);
  }
}

void Settings::ResetSwipes() {
  swipe_left = kDefaults.swipe_left;
  swipe_right = kDefaults.swipe_right;
  swipe_up = kDefaults.swipe_up;
  swipe_down = kDefaults.swipe_down;
}

bool Settings::Load() {
  PERF_SCOPE("Settings::Load");
  hal::CheckSdTaskInvariant("Settings::Load");
  FILE* f = fopen(SettingsPath().c_str(), "r");
  if (!f) return false;
  char line[320];
  while (fgets(line, sizeof(line), f)) {
    const char* eq = strchr(line, '=');
    if (!eq) continue;
    const std::string key = Trim(std::string(line, eq - line).c_str());
    const std::string value = Trim(eq + 1);
    if (key == "line_spacing") {
      line_spacing = ClampLineSpacing(strtof(value.c_str(), nullptr));
    } else if (key == "font_path") {
      body_font_path = value;
    } else if (key == "body_font_size") {
      body_font_size = ClampBodyFontSize(atoi(value.c_str()));
    } else if (key == "margin") {
      margin = ClampMargin(atoi(value.c_str()));
    } else if (key == "paragraph_indent") {
      paragraph_indent = ParseBool(value);
    } else if (key == "line_underline") {
      line_underline = ParseBool(value);
    } else if (key == "orientation") {
      orientation = static_cast<Orientation>(Clamp(atoi(value.c_str()), 0, 3));
    } else if (key == "full_refresh_pages") {
      full_refresh_pages = ClampFullRefreshPages(atoi(value.c_str()));
    } else if (key == "tap_layout") {
      tap_layout = static_cast<TapLayout>(Clamp(atoi(value.c_str()), 0, 1));
    } else if (key == "tap_columns") {
      ParseActions(value, tap_columns, 3);
    } else if (key == "tap_grid") {
      ParseActions(value, tap_grid, 9);
    } else if (key == "swipe_left") {
      swipe_left = ParseAction(atoi(value.c_str()));
    } else if (key == "swipe_right") {
      swipe_right = ParseAction(atoi(value.c_str()));
    } else if (key == "swipe_up") {
      swipe_up = ParseAction(atoi(value.c_str()));
    } else if (key == "swipe_down") {
      swipe_down = ParseAction(atoi(value.c_str()));
    } else if (key == "auto_turn") {
      auto_turn = ParseBool(value);
    } else if (key == "auto_turn_seconds") {
      auto_turn_seconds = ClampAutoTurnSeconds(atoi(value.c_str()));
    } else if (key == "wheel_inverted") {
      wheel_inverted = ParseBool(value);
    } else if (key == "ui_font_path") {
      ui_font_path = value.empty() ? "builtin" : value;
    } else if (key == "lock_screen_enabled") {
      lock_screen_enabled = ParseBool(value);
    } else if (key == "lock_screen_path") {
      lock_screen_path = value;
    } else if (key == "sleep_mode") {
      sleep_mode = static_cast<SleepMode>(Clamp(atoi(value.c_str()), 0, 1));
    } else if (key == "language") {
      language = static_cast<i18n::Language>(
          Clamp(atoi(value.c_str()), 0, i18n::kLanguageCount - 1));
    } else if (key == "bookshelf_sort") {
      bookshelf_sort = static_cast<BookshelfSort>(
          Clamp(atoi(value.c_str()), 0, kBookshelfSortCount - 1));
    } else if (key == "read_seq") {
      read_seq = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
    }
  }
  fclose(f);
  return true;
}

bool Settings::Save() const {
  PERF_SCOPE("Settings::Save");
  hal::CheckSdTaskInvariant("Settings::Save");
  FILE* f = fopen(SettingsPath().c_str(), "w");
  if (!f) {
    hal::ReportSdError();
    return false;
  }
  // "font_path" is the body font; the key name predates the UI font setting.
  fprintf(f, "line_spacing=%.1f\n", line_spacing);
  fprintf(f, "font_path=%s\n", body_font_path.c_str());
  fprintf(f, "body_font_size=%d\n", body_font_size);
  fprintf(f, "margin=%d\n", margin);
  fprintf(f, "paragraph_indent=%d\n", paragraph_indent ? 1 : 0);
  fprintf(f, "line_underline=%d\n", line_underline ? 1 : 0);
  fprintf(f, "orientation=%d\n", static_cast<int>(orientation));
  fprintf(f, "full_refresh_pages=%d\n", full_refresh_pages);
  fprintf(f, "tap_layout=%d\n", static_cast<int>(tap_layout));
  PrintActions(f, "tap_columns", tap_columns, 3);
  PrintActions(f, "tap_grid", tap_grid, 9);
  fprintf(f, "swipe_left=%d\n", static_cast<int>(swipe_left));
  fprintf(f, "swipe_right=%d\n", static_cast<int>(swipe_right));
  fprintf(f, "swipe_up=%d\n", static_cast<int>(swipe_up));
  fprintf(f, "swipe_down=%d\n", static_cast<int>(swipe_down));
  fprintf(f, "auto_turn=%d\n", auto_turn ? 1 : 0);
  fprintf(f, "auto_turn_seconds=%d\n", auto_turn_seconds);
  fprintf(f, "wheel_inverted=%d\n", wheel_inverted ? 1 : 0);
  fprintf(f, "ui_font_path=%s\n", ui_font_path.c_str());
  fprintf(f, "lock_screen_enabled=%d\n", lock_screen_enabled ? 1 : 0);
  fprintf(f, "lock_screen_path=%s\n", lock_screen_path.c_str());
  fprintf(f, "sleep_mode=%d\n", static_cast<int>(sleep_mode));
  fprintf(f, "language=%d\n", static_cast<int>(language));
  fprintf(f, "bookshelf_sort=%d\n", static_cast<int>(bookshelf_sort));
  fprintf(f, "read_seq=%u\n", static_cast<unsigned>(read_seq));
  return fclose(f) == 0;
}

}  // namespace core
}  // namespace reader
