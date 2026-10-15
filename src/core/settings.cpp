#include "core/settings.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/perf_trace.h"

namespace reader {
namespace core {
namespace {

// The SD card is mounted at /sd for POSIX access (see src/main.cpp).
constexpr char kSettingsPath[] = "/sd/configs/settings.cfg";

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

}  // namespace

int ClampBodyFontSize(int size) {
  size = Clamp(size, kMinBodyFontSize, kMaxBodyFontSize);
  return size - (size - kMinBodyFontSize) % kBodyFontSizeStep;
}

float ClampLineSpacing(float spacing) {
  if (!(spacing >= kMinLineSpacing)) return kMinLineSpacing;  // Also NaN.
  if (spacing > kMaxLineSpacing) return kMaxLineSpacing;
  const float steps = std::round((spacing - kMinLineSpacing) / kLineSpacingStep);
  return kMinLineSpacing + steps * kLineSpacingStep;
}

int ClampMargin(int margin) { return Clamp(margin, kMinMargin, kMaxMargin); }

int ClampFullRefreshPages(int pages) {
  return Clamp(pages, kMinFullRefreshPages, kMaxFullRefreshPages);
}

bool Settings::Load() {
  PERF_SCOPE("Settings::Load");
  FILE* f = fopen(kSettingsPath, "r");
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
    } else if (key == "ui_font_path") {
      ui_font_path = value.empty() ? "builtin" : value;
    } else if (key == "lock_screen_enabled") {
      lock_screen_enabled = ParseBool(value);
    } else if (key == "lock_screen_path") {
      lock_screen_path = value;
    }
  }
  fclose(f);
  return true;
}

bool Settings::Save() const {
  PERF_SCOPE("Settings::Save");
  FILE* f = fopen(kSettingsPath, "w");
  if (!f) return false;
  // "font_path" is the body font; the key name predates the UI font setting.
  fprintf(f, "line_spacing=%.1f\n", line_spacing);
  fprintf(f, "font_path=%s\n", body_font_path.c_str());
  fprintf(f, "body_font_size=%d\n", body_font_size);
  fprintf(f, "margin=%d\n", margin);
  fprintf(f, "paragraph_indent=%d\n", paragraph_indent ? 1 : 0);
  fprintf(f, "line_underline=%d\n", line_underline ? 1 : 0);
  fprintf(f, "orientation=%d\n", static_cast<int>(orientation));
  fprintf(f, "full_refresh_pages=%d\n", full_refresh_pages);
  fprintf(f, "ui_font_path=%s\n", ui_font_path.c_str());
  fprintf(f, "lock_screen_enabled=%d\n", lock_screen_enabled ? 1 : 0);
  fprintf(f, "lock_screen_path=%s\n", lock_screen_path.c_str());
  return fclose(f) == 0;
}

}  // namespace core
}  // namespace reader
