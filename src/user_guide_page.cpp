#include "user_guide_page.h"
#include "ui_manager.h"
#include "user_guide_text.h"
#include "utf8_utils.h"

#include <M5Unified.h>

extern UIManager ui;

void UserGuidePage::init() {
  current_page_idx = 0;
  pages.clear();
}

void UserGuidePage::enter() {
  current_page_idx = 0;
}

void UserGuidePage::exit() {
}

bool UserGuidePage::cannotStartLine(uint32_t unicode) {
    switch (unicode) {
        case 0x3002: // 。
        case 0x3001: // 、
        case 0xFF0C: // ，
        case 0xFF1B: // ；
        case 0xFF1A: // ：
        case 0xFF1F: // ？
        case 0xFF01: // ！
        case 0xFF09: // ）
        case 0x3011: // 】
        case 0x300F: // 』
        case 0x300D: // 」
        case '!':
        case '?':
        case ',':
        case '.':
        case ';':
        case ':':
        case ')':
        case ']':
        case '}':
            return true;
        default:
            return false;
    }
}

void UserGuidePage::paginate(LGFX_Sprite& canvas) {
  pages.clear();
  current_page_idx = 0;
  
  std::string text = USER_GUIDE_TEXT;
  ui.buildFontCache(text); // Build cache for the whole text
  
  int font_size = ui.getFontSize();
  float line_spacing = 1.5f; // Force 1.5 line spacing for user guide
  int line_height = font_size * line_spacing;
  
  int max_w = canvas.width() - 40; // 20px padding on each side
  int bar_h = ui.getHeaderHeight();
  int bottom_h = ui.getFooterHeight();
  int max_h = canvas.height() - bar_h - bottom_h - 40;
  
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
  const uint8_t* end = p + text.size();
  const uint8_t* page_start = p;
  
  while (p < end) {
    int cursor_x = 0;
    int cursor_y = 0;
    const uint8_t* page_end = p;
    const uint8_t* last_p = p;
    std::string paginated_text = "";
    
    while (p < end) {
      int bytes = 0;
      uint32_t u = Utf8Utils::getNextCharUnicode(p, end, bytes);
      
      if (bytes == 0) {
          p++;
          page_end = p;
          continue;
      }
      
      if (u == '\n') {
        paginated_text.append(reinterpret_cast<const char*>(last_p), p - last_p + bytes);
        cursor_x = 0;
        cursor_y += line_height;
        p += bytes;
        last_p = p;
        page_end = p;
        continue;
      }
      if (u == '\r') {
          p += bytes;
          continue;
      }

      // Measure character
      std::string char_str(reinterpret_cast<const char*>(p), bytes);
      int char_w = ui.getStringWidth(char_str);

      if (cursor_x + char_w > max_w) {
        if (cursor_x == 0) {
            cursor_x += char_w;
            p += bytes;
            page_end = p;
            continue;
        } else if (cannotStartLine(u) && cursor_x + char_w <= max_w * 1.15f) {
            cursor_x += char_w;
            p += bytes;
            page_end = p;
            continue;
        } else {
            paginated_text.append(reinterpret_cast<const char*>(last_p), p - last_p);
            paginated_text.append("\n");
            cursor_x = 0;
            cursor_y += line_height;
            last_p = p;
            continue;
        }
      }

      if (cursor_y + font_size > max_h) {
        break;
      }

      cursor_x += char_w;
      p += bytes;
      page_end = p; 
    }
    
    paginated_text.append(reinterpret_cast<const char*>(last_p), page_end - last_p);
    pages.push_back(paginated_text);
    
    p = page_end;
  }
}

void UserGuidePage::draw(LGFX_Sprite& canvas) {
  if (pages.empty()) {
    paginate(canvas);
  }
  
  std::string cache_chars = "返回使用说明0123456789%·";
  if (current_page_idx >= 0 && current_page_idx < pages.size()) {
    cache_chars += pages[current_page_idx];
  }
  ui.buildFontCache(cache_chars);
  
  canvas.fillScreen(TFT_WHITE);
  
  int font_size = ui.getFontSize();
  int screen_w = canvas.width();
  int screen_h = canvas.height();
  
  // 1. Draw Header (Permanently visible)
  int bar_h = ui.getHeaderHeight();
  int text_y_offset = (bar_h - font_size) / 2;
  int col_w = screen_w / 2;
  
  // Top Left: "返回"
  std::string back_text = "返回";
  int back_w = ui.getStringWidth(back_text);
  ui.drawCachedString(back_text, col_w / 2 - back_w / 2, text_y_offset);
  
  // Draw vertical separator for "返回" button
  canvas.drawLine(col_w, 0, col_w, bar_h, TFT_BLACK);

  // Top Right: Battery
  ui.drawBattery(canvas, canvas.width() - 20, text_y_offset);
  
  // Thick line at bottom of header
  canvas.fillRect(0, bar_h - 4, screen_w, 4, TFT_BLACK);
  
  // 2. Draw Content
  if (current_page_idx >= 0 && current_page_idx < pages.size()) {
    ui.drawCachedString(pages[current_page_idx], 20, bar_h + 20);
  }
  
  // 3. Draw Bottom Bar (Permanently visible)
  int bottom_h = ui.getFooterHeight();
  int bottom_y = screen_h - bottom_h;
  canvas.fillRect(0, bottom_y, screen_w, 4, TFT_BLACK); // Thick line at top of bar
  
  // Draw progress centered in 100px
  char prog_str[64];
  snprintf(prog_str, sizeof(prog_str), "使用说明 · %d/%d", current_page_idx + 1, (int)pages.size());
  std::string prog_text = prog_str;
  int prog_w = ui.getStringWidth(prog_text);
  ui.drawCachedString(prog_text, (screen_w - prog_w) / 2, bottom_y + (bottom_h - font_size) / 2);
}

State UserGuidePage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type == MessageType::TOUCH) {
    int x = msg.data.touch.x;
    int y = msg.data.touch.y;
    
    int screen_w = M5.Display.width();
    int screen_h = M5.Display.height();
    
    int font_size = ui.getFontSize();
    int bar_h = ui.getHeaderHeight();
    int bottom_h = ui.getFooterHeight();
    
    if (y < bar_h) { // Top Bar
      if (x < screen_w / 2) { // "返回" button
        return State::SETTING;
      }
    } else if (y < screen_h - bottom_h) { // Middle Area (Reading area)
      if (x < screen_w / 3) { // Prev Page
        if (current_page_idx > 0) {
          current_page_idx--;
          ui.drawCurrentPage();
        }
      } else if (x > 2 * screen_w / 3) { // Next Page
        if (current_page_idx + 1 < pages.size()) {
          current_page_idx++;
          ui.drawCurrentPage();
        }
      }
    }
  } else if (msg.type == MessageType::WHEEL) {
    int delta = msg.data.wheel.delta;
    if (delta > 0) { // Next Page
      if (current_page_idx + 1 < pages.size()) {
        current_page_idx++;
        ui.drawCurrentPage();
      }
    } else if (delta < 0) { // Prev Page
      if (current_page_idx > 0) {
        current_page_idx--;
        ui.drawCurrentPage();
      }
    }
  }
  return State::USER_GUIDE;
}
