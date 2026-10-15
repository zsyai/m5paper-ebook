#pragma once

// Everything that decides how book text is laid out and drawn.
//
// Built from core::Settings. BookReader paginates with it and the reader
// view draws with it; the fields that change page breaks also go into the
// page index key (see BookReader::makeIndexKey).

#include <string>

#include "core/settings.h"

namespace reader {
namespace book {

// Text rectangle on the canvas, in canvas pixels.
struct TextArea {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct ReadingStyle {
  std::string font_path = "builtin";
  float line_spacing = 2.0f;
  int margin = 20;
  bool paragraph_indent = false;
  bool line_underline = false;
  core::Orientation orientation = core::Orientation::kUp;

  static ReadingStyle FromSettings(const core::Settings& settings);

  // Distance between baselines for glyphs of `font_size` pixels.
  int LineHeight(int font_size) const {
    return static_cast<int>(font_size * line_spacing);
  }

  // The canvas minus the margin on every side (at least 1x1).
  TextArea Area(int canvas_w, int canvas_h) const;
};

}  // namespace book
}  // namespace reader
