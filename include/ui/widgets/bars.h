#pragma once

// Bars and boxes shared by several pages.

#include <string>
#include <vector>

#include "ui/painter.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

// Page header of `bar_h` pixels: `title` at the left, the battery at the
// right, and a 4px line at the bottom.
void DrawHeader(UiPainter& p, const std::string& title, int bar_h,
                int battery_level);

// Sub-page header of `bar_h` pixels: a "返回" button in the left half, a
// vertical divider, the battery at the right, and a 4px line at the bottom.
// Returns the "返回" button hit rectangle.
Rect DrawBackHeader(UiPainter& p, int bar_h, int battery_level);

// Bottom bar of `bar_h` pixels: a white background with a 4px line on top,
// then `items` in equal columns with thin separators.
void DrawBottomBar(UiPainter& p, const std::vector<std::string>& items,
                   int bar_h);

// Returns the 0-based column index hit in a bottom bar of `bar_h` pixels with
// `count` equal columns, or -1 if (x, y) is outside the bar.
int BottomBarHit(const LGFX_Sprite& canvas, int bar_h, int count, int x, int y);

// "78%" followed by a battery icon, right edge at `right_x`, text top at
// `y`.
void DrawBattery(UiPainter& p, int right_x, int y, int level);

// One or more centered lines of text in a bordered white box at the center of
// the canvas.
void DrawMessageBox(UiPainter& p, const std::vector<std::string>& lines);

// One line of text in a bordered box at the center of the canvas.
void DrawPromptBox(UiPainter& p, const std::string& text);

}  // namespace ui
}  // namespace reader
