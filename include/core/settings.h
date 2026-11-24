#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_CORE_SETTINGS_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_CORE_SETTINGS_H_

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
constexpr float kLineSpacingStep = 0.1f;
constexpr int kMinMargin = 0;
constexpr int kMaxMargin = 80;
constexpr int kMinFullRefreshPages = 1;
constexpr int kMaxFullRefreshPages = 99;
constexpr int kMinAutoTurnSeconds = 5;
constexpr int kMaxAutoTurnSeconds = 600;

// What a tap zone or a swipe does on the reading page. The values are
// stored in settings.cfg, so keep them stable.
enum class TouchAction : uint8_t {
  kNone = 0,
  kPrevPage = 1,
  kNextPage = 2,
  kMenu = 3,
  kFullRefresh = 4,
  kToggleAutoTurn = 5,  // Turns the auto page turn on or off.
};
constexpr int kTouchActionCount = 6;

// What 休眠 does.
enum class SleepMode : uint8_t {
  // Power off (deep sleep with the main power released); the wheel must be
  // held ~2 s to power on, and boot restores the open book.
  kDeep = 0,
  // Board stays powered and the ESP32 light-sleeps; pressing or rolling the
  // wheel resumes at once. Uses more standby power.
  kLight = 1,
};

// How the reading page is split into tap zones.
enum class TapLayout : uint8_t {
  kColumns = 0,  // Three columns.
  kGrid = 1,     // 3 x 3 grid.
};

// Bookshelf ordering mode persisted in settings.cfg.
enum class BookshelfSort : uint8_t {
  kRecentRead = 0,       // Strict recent-open order (including completed).
  kTitle = 1,            // Title Pinyin / alphabetical ascending.
  kReadingProgress = 2,  // Last read -> In-progress (%) -> Unread -> Done.
  kRecentImport = 3,     // Import timestamp / directory order descending.
};
constexpr int kBookshelfSortCount = 4;

// Clamps to the range and snaps down to the step.
int ClampBodyFontSize(int size);
// Clamps to the range and rounds to the nearest step.
float ClampLineSpacing(float spacing);
int ClampMargin(int margin);
int ClampFullRefreshPages(int pages);
int ClampAutoTurnSeconds(int seconds);

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

  // ---- Reading controls ----
  // Tap zones in the reading direction: three columns (left to right) or
  // a 3 x 3 grid (row by row). Both are kept so switching layouts does not
  // lose either one.
  TapLayout tap_layout = TapLayout::kColumns;
  TouchAction tap_columns[3] = {TouchAction::kPrevPage, TouchAction::kMenu,
                                TouchAction::kNextPage};
  TouchAction tap_grid[9] = {
      TouchAction::kPrevPage, TouchAction::kMenu, TouchAction::kNextPage,
      TouchAction::kPrevPage, TouchAction::kMenu, TouchAction::kNextPage,
      TouchAction::kPrevPage, TouchAction::kMenu, TouchAction::kNextPage};
  TouchAction swipe_left = TouchAction::kNextPage;
  TouchAction swipe_right = TouchAction::kPrevPage;
  TouchAction swipe_up = TouchAction::kToggleAutoTurn;
  TouchAction swipe_down = TouchAction::kMenu;
  // Turn to the next page every auto_turn_seconds while reading.
  bool auto_turn = false;
  int auto_turn_seconds = 30;
  // Swaps the wheel's previous / next page directions on the reading page.
  bool wheel_inverted = false;

  // Action of a tap at (x, y) on a `w` x `h` reading page.
  TouchAction TapAction(int x, int y, int w, int h) const;
  // Restores the default zones of the current tap layout.
  void ResetTapZones();
  void ResetSwipes();

  // ---- System ----
  // UI font: "builtin" or a .ttf under /fonts.
  std::string ui_font_path = "builtin";
  bool lock_screen_enabled = false;
  std::string lock_screen_path;
  SleepMode sleep_mode = SleepMode::kDeep;
  BookshelfSort bookshelf_sort = BookshelfSort::kRecentRead;
  // Monotonic sequence incremented each time a book is opened (`last_read=` in
  // per-book `.cfg`), avoiding reliance on an uncalibrated hardware RTC.
  uint32_t read_seq = 0;

  // Returns false if the file is missing or unreadable; fields that were
  // not read keep their current values.
  bool Load();
  bool Save() const;
};

}  // namespace core
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_CORE_SETTINGS_H_
