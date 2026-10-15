#include "ui/widgets/bars.h"

#include <algorithm>
#include <cstdio>

namespace reader {
namespace ui {

void DrawHeader(UiPainter& p, const std::string& title, int bar_h,
                int battery_level) {
  LGFX_Sprite& canvas = p.canvas();
  const int text_y = p.CenterY(0, bar_h);
  p.Prepare(title);
  p.Text(title, 20, text_y);
  DrawBattery(p, canvas.width() - 20, text_y, battery_level);
  canvas.fillRect(0, bar_h - 4, canvas.width(), 4, TFT_BLACK);
}

Rect DrawBackHeader(UiPainter& p, int bar_h, int battery_level) {
  LGFX_Sprite& canvas = p.canvas();
  const int screen_w = canvas.width();
  const int half_w = screen_w / 2;
  canvas.fillRect(0, 0, screen_w, bar_h, TFT_WHITE);
  const Rect back{0, 0, half_w, bar_h};
  p.TextIn("返回", back);
  canvas.drawFastVLine(half_w, 0, bar_h, TFT_BLACK);
  DrawBattery(p, screen_w - 20, p.CenterY(0, bar_h), battery_level);
  canvas.fillRect(0, bar_h - 4, screen_w, 4, TFT_BLACK);
  return back;
}

void DrawBottomBar(UiPainter& p, const std::vector<std::string>& items,
                   int bar_h) {
  LGFX_Sprite& canvas = p.canvas();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const int top = screen_h - bar_h;
  canvas.fillRect(0, top, screen_w, bar_h, TFT_WHITE);
  canvas.fillRect(0, top, screen_w, 4, TFT_BLACK);
  if (items.empty()) return;

  const int text_y = top + (bar_h - p.font_size()) / 2;
  const int col_w = screen_w / static_cast<int>(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    p.Prepare(items[i]);
    const int x = static_cast<int>(i) * col_w + col_w / 2 -
                  p.Width(items[i]) / 2;
    p.Text(items[i], x, text_y);
    if (i > 0) {
      canvas.drawFastVLine(static_cast<int>(i) * col_w, top, bar_h, TFT_BLACK);
    }
  }
}

int BottomBarHit(const LGFX_Sprite& canvas, int bar_h, int count, int x,
                 int y) {
  if (count <= 0 || y < canvas.height() - bar_h) return -1;
  const int col = x * count / canvas.width();
  return std::min(count - 1, std::max(0, col));
}

void DrawBattery(UiPainter& p, int right_x, int y, int level) {
  LGFX_Sprite& canvas = p.canvas();
  char text[16];
  snprintf(text, sizeof(text), "%d%%", level);
  p.Prepare(text);
  const int text_w = p.Width(text);

  const int font_size = p.font_size();
  const int icon_h = font_size * 0.75;
  const int icon_w = icon_h * 2.1;
  const int cap_h = icon_h * 0.4;
  const int cap_w = icon_h * 0.15;
  const int start_x = right_x - (text_w + 10 + icon_w + cap_w);
  p.Text(text, start_x, y);

  const int icon_x = start_x + text_w + 10;
  const int icon_y = y + (font_size - icon_h) / 2;
  canvas.drawRect(icon_x, icon_y, icon_w, icon_h, TFT_BLACK);
  canvas.fillRect(icon_x + icon_w, icon_y + (icon_h - cap_h) / 2, cap_w, cap_h,
                  TFT_BLACK);
  const int fill_w = level * (icon_w - 4) / 100;
  if (fill_w > 0) {
    canvas.fillRect(icon_x + 2, icon_y + 2, fill_w, icon_h - 4, TFT_BLACK);
  }
}

void DrawMessageBox(UiPainter& p, const std::vector<std::string>& lines) {
  if (lines.empty()) return;
  LGFX_Sprite& canvas = p.canvas();
  const int font_size = p.font_size();
  constexpr int kPadding = 20;
  constexpr int kLineGap = 20;

  int max_w = 0;
  for (const auto& line : lines) {
    p.Prepare(line);
    max_w = std::max(max_w, p.Width(line));
  }
  const int n = static_cast<int>(lines.size());
  const int box_w = max_w + kPadding * 2;
  const int box_h = n * font_size + (n - 1) * kLineGap + kPadding * 2;
  const int box_x = (canvas.width() - box_w) / 2;
  const int box_y = (canvas.height() - box_h) / 2;
  canvas.fillRect(box_x, box_y, box_w, box_h, TFT_WHITE);
  canvas.drawRect(box_x, box_y, box_w, box_h, TFT_BLACK);
  for (int i = 0; i < n; ++i) {
    const int line_w = p.Width(lines[i]);
    const int text_x = box_x + (box_w - line_w) / 2;
    const int text_y = box_y + kPadding + i * (font_size + kLineGap);
    p.Text(lines[i], text_x, text_y);
  }
}

void DrawPromptBox(UiPainter& p, const std::string& text) {
  DrawMessageBox(p, {text});
}

}  // namespace ui
}  // namespace reader
