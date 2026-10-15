#include "ui/widgets/bars.h"

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

void DrawBottomBar(UiPainter& p, const std::vector<std::string>& items,
                   int bar_h) {
  LGFX_Sprite& canvas = p.canvas();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const int top = screen_h - bar_h;
  canvas.fillRect(0, top, screen_w, 4, TFT_BLACK);
  if (items.empty()) return;

  const int text_y = top + (bar_h - p.font_size()) / 2;
  const int col_w = screen_w / items.size();
  for (size_t i = 0; i < items.size(); ++i) {
    p.Prepare(items[i]);
    const int x = i * col_w + col_w / 2 - p.Width(items[i]) / 2;
    p.Text(items[i], x, text_y);
    if (i > 0) canvas.drawLine(i * col_w, top, i * col_w, screen_h, TFT_BLACK);
  }
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

void DrawPromptBox(UiPainter& p, const std::string& text) {
  LGFX_Sprite& canvas = p.canvas();
  p.Prepare(text);
  const int font_size = p.font_size();
  const int text_w = p.Width(text);
  const int box_w = text_w + 40;
  const int box_h = font_size + 40;
  const int box_x = (canvas.width() - box_w) / 2;
  const int box_y = (canvas.height() - box_h) / 2;
  canvas.fillRect(box_x, box_y, box_w, box_h, TFT_WHITE);
  canvas.drawRect(box_x, box_y, box_w, box_h, TFT_BLACK);
  p.Text(text, box_x + (box_w - text_w) / 2, box_y + (box_h - font_size) / 2);
}

}  // namespace ui
}  // namespace reader
