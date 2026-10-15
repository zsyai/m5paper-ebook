#pragma once

// Bars and boxes shared by several pages.

#include <string>
#include <vector>

#include "ui/painter.h"

namespace reader {
namespace ui {

// Page header of `bar_h` pixels: `title` at the left, the battery at the
// right, and a 4px line at the bottom.
void DrawHeader(UiPainter& p, const std::string& title, int bar_h,
                int battery_level);

// Bottom bar of `bar_h` pixels: a 4px line on top, then `items` in equal
// columns with thin separators.
void DrawBottomBar(UiPainter& p, const std::vector<std::string>& items,
                   int bar_h);

// "78%" followed by a battery icon, right edge at `right_x`, text top at
// `y`.
void DrawBattery(UiPainter& p, int right_x, int y, int level);

// One line of text in a bordered box at the center of the canvas.
void DrawPromptBox(UiPainter& p, const std::string& text);

}  // namespace ui
}  // namespace reader
