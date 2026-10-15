#pragma once

// Global settings stored in /configs/settings.cfg as "key=value" lines.
//
// Reading settings apply to every book; per-book state (position,
// bookmarks) stays in /configs/<book>.cfg (BookReader). Missing keys keep
// their defaults, and every value is clamped to its range on load, so old
// or hand-edited files are safe to read.

#include <cstdint>
#include <string>

namespace reader {
namespace core {

// Reading direction: where the top of the text points on the physical
// screen (portrait device, wheel on the right).
enum class Orientation : uint8_t {
  kUp = 0,     // Portrait, as today.
  kRight = 1,  // Landscape, text top towards the wheel side.
  kDown = 2,   // Portrait, upside down.
  kLeft = 3,   // Landscape, text top away from the wheel side.
};

// Value ranges shared by the settings pages and Settings::Load().
constexpr int kMinBodyFontSize = 20;
constexpr int kMaxBodyFontSize = 60;
constexpr int kBodyFontSizeStep = 2;
constexpr float kMinLineSpacing = 1.0f;
constexpr float kMaxLineSpacing = 3.0f;
constexpr float kLineSpacingStep = 0.5f;
constexpr int kMinMargin = 0;
constexpr int kMaxMargin = 80;
constexpr int kMinFullRefreshPages = 1;
constexpr int kMaxFullRefreshPages = 99;

// Clamps to the range and snaps down to the step.
int ClampBodyFontSize(int size);
// Clamps to the range and rounds to the nearest step.
float ClampLineSpacing(float spacing);
int ClampMargin(int margin);
int ClampFullRefreshPages(int pages);

struct Settings {
  // ---- Reading ----
  // Body font: "builtin" or a .bin / .ttf path under /fonts.
  std::string body_font_path = "/font.bin";
  // Pixel size for .ttf body fonts (.bin fonts have a fixed size).
  int body_font_size = 30;
  float line_spacing = 2.0f;
  // Blank border around the text on all four sides, in pixels.
  int margin = 20;
  // On: every paragraph starts with a 2-character indent. Off: keep the
  // text as written.
  bool paragraph_indent = false;
  // Dashed line under each text line (drawing only, no layout change).
  bool line_underline = false;
  Orientation orientation = Orientation::kUp;
  // Full (GC16) refresh every N page turns to clear ghosting.
  int full_refresh_pages = 5;

  // ---- System ----
  // UI font: "builtin" or a .ttf under /fonts.
  std::string ui_font_path = "builtin";
  bool lock_screen_enabled = false;
  std::string lock_screen_path;

  // Returns false if the file is missing or unreadable; fields that were
  // not read keep their current values.
  bool Load();
  bool Save() const;
};

}  // namespace core
}  // namespace reader
