#include "ui/painter.h"

#include "ui/text_renderer.h"

namespace reader {
namespace ui {

void UiPainter::Prepare(const std::string& text) {
  cache_.build(text, font_path_);
}

int UiPainter::Width(const std::string& text) {
  return TextWidth(cache_, text);
}

void UiPainter::Text(const std::string& text, int x, int y, bool inverted) {
  DrawText(canvas_, cache_, text, x, y, font_size() * 1.5, inverted);
}

void UiPainter::TextIn(const std::string& text, const Rect& r, bool inverted) {
  Prepare(text);
  Text(text, r.x + (r.w - Width(text)) / 2, CenterY(r.y, r.h), inverted);
}

}  // namespace ui
}  // namespace reader
