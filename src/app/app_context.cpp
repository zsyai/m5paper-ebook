#include "app/app_context.h"

#include <string>

namespace reader {
namespace app {

void AppContext::SetUiFontPath(const std::string& path) {
  settings.ui_font_path = path;
  frame.SetUiFontPath(fonts.EffectiveUiFontPath(settings));
  fonts.OnUiFontChanged();
}

void AppContext::SetBodyFontPath(const std::string& path) {
  settings.body_font_path = path;
  book.ApplyStyle(settings);
  fonts.OnBodyFontChanged();
  book.MarkLayoutDirty();
}

void AppContext::SetBodyFontSize(int size) {
  settings.body_font_size = core::ClampBodyFontSize(size);
  fonts.OnBodyFontSizeChanged(settings.body_font_size);
  book.MarkLayoutDirty();
}

void AppContext::SetLineSpacing(float factor) {
  settings.line_spacing = core::ClampLineSpacing(factor);
  book.ApplyStyle(settings);
  book.MarkLayoutDirty();
}

void AppContext::SetMargin(int margin) {
  settings.margin = core::ClampMargin(margin);
  book.ApplyStyle(settings);
  book.MarkLayoutDirty();
}

void AppContext::SetParagraphIndent(bool on) {
  settings.paragraph_indent = on;
  book.ApplyStyle(settings);
  book.MarkLayoutDirty();
}

void AppContext::SetLineUnderline(bool on) {
  settings.line_underline = on;
  book.ApplyStyle(settings);
}

void AppContext::SetOrientation(core::Orientation orientation) {
  settings.orientation = orientation;
  book.ApplyStyle(settings);
  if (sync_canvas_rotation) sync_canvas_rotation();
  book.MarkLayoutDirty();
}

void AppContext::SetFullRefreshPages(int pages) {
  settings.full_refresh_pages = core::ClampFullRefreshPages(pages);
}

}  // namespace app
}  // namespace reader
