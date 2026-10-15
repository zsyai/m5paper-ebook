#include "book/reading_style.h"

#include <algorithm>

namespace reader {
namespace book {

ReadingStyle ReadingStyle::FromSettings(const core::Settings& settings) {
  ReadingStyle style;
  style.font_path = settings.body_font_path;
  style.line_spacing = settings.line_spacing;
  style.margin = settings.margin;
  style.paragraph_indent = settings.paragraph_indent;
  style.line_underline = settings.line_underline;
  style.orientation = settings.orientation;
  return style;
}

TextArea ReadingStyle::Area(int canvas_w, int canvas_h) const {
  TextArea area;
  area.x = margin;
  area.y = margin;
  area.w = std::max(canvas_w - 2 * margin, 1);
  area.h = std::max(canvas_h - 2 * margin, 1);
  return area;
}

}  // namespace book
}  // namespace reader
