#include "setting_page.h"
#include "ui_manager.h"
#include "utf8_utils.h"
#include "font_manager.h"

#include <M5Unified.h>
#include <SD.h>

extern UIManager ui;

void SettingPage::init() {
}

void SettingPage::scanFonts() {
  available_fonts.clear();
  std::vector<std::string> fonts = FontManager::scanAvailableFonts();
  for (const auto& f : fonts) {
      available_fonts.push_back(f.c_str());
  }
  
  if (available_fonts.empty()) {
      available_fonts.push_back("/font.bin");
  }
}

void SettingPage::enter() {
  ui.loadGlobalConfig();
  current_line_spacing = ui.getLineSpacingFactor();
  initial_font_size = ui.getFontSize();
  scanFonts();
  
  std::string current_font = ui.getFontPath();
  current_font_index = 0;
  for (size_t i = 0; i < available_fonts.size(); ++i) {
      if (available_fonts[i] == current_font.c_str()) {
          current_font_index = i;
          break;
      }
  }
}

void SettingPage::exit() {
}

void SettingPage::draw(LGFX_Sprite& canvas) {
  ui.buildFontCache(
      "设置剩余电量行间距1.52.0返回传书书架休眠0123456789%"
      "清理过期阅读记录使用说明阅读字体锁屏壁纸开关选择当前件启文加载中."
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_<>");
  canvas.fillScreen(TFT_WHITE);
  
  int screen_w = canvas.width();
  int screen_h = canvas.height();
  int font_size = ui.getFontSize();

  int fs = font_size;
  int padding_x = 20;
  int line_h = fs * 3.2;  // Module height

  // Header
  int bar_h = ui.getHeaderHeight();
  int header_y = (bar_h - fs) / 2;
  ui.drawCachedString("设置", 20, header_y);
  
  // Battery
  ui.drawBattery(canvas, canvas.width() - 20, header_y);
  
  // Draw header separator line
  int base_y = bar_h - 4;  // Consistent with other pages
  canvas.fillRect(0, base_y, screen_w, 4, TFT_BLACK);

  // Helper lambda for vertical centering
  auto get_centered_y = [&](int top_y, int height, int element_h) {
    return top_y + (height - element_h) / 2;
  };

  // Module 1: 行间距
  int mod1_top = base_y;
  int mod1_h = line_h;
  int item_y = get_centered_y(mod1_top, mod1_h, fs);

  ui.drawCachedString("行间距", padding_x, item_y);

  // Radio buttons for 1.5 and 2.0
  int radio_y = mod1_top + mod1_h / 2;
  int r = fs * 0.4;

  // 1.5
  int x1 = padding_x + ui.getStringWidth("行间距") + fs * 2.0;
  if (current_line_spacing == 1.5f) {
    canvas.fillCircle(x1, radio_y, r, TFT_BLACK);
    canvas.fillCircle(x1, radio_y, r - 4, TFT_WHITE);
    canvas.fillCircle(x1, radio_y, r - 8, TFT_BLACK);  // Inner dot
  } else {
    canvas.drawCircle(x1, radio_y, r, TFT_BLACK);
  }
  int txt_w = ui.getStringWidth("1.5");
  ui.drawCachedString("1.5", x1 + r + 10, item_y);

  // 2.0
  int x2 = x1 + fs * 5.0;
  if (current_line_spacing == 2.0f) {
    canvas.fillCircle(x2, radio_y, r, TFT_BLACK);
    canvas.fillCircle(x2, radio_y, r - 4, TFT_WHITE);
    canvas.fillCircle(x2, radio_y, r - 8, TFT_BLACK);
  } else {
    canvas.drawCircle(x2, radio_y, r, TFT_BLACK);
  }
  ui.drawCachedString("2.0", x2 + r + 10, item_y);

  // Separator after Module 1
  canvas.drawFastHLine(padding_x, mod1_top + mod1_h, screen_w - 2 * padding_x,
                       TFT_BLACK);

  // Module 2: 字体
  int mod2_top = mod1_top + mod1_h;
  int mod2_h = line_h;
  item_y = get_centered_y(mod2_top, mod2_h, fs);

  ui.drawCachedString("字体", padding_x, item_y);

  int right_arrow_center_x =
      screen_w - padding_x - fs * 1.5;  // Aligned with button center
  std::string font_name = ui.getFontName();
  if (font_name.empty()) font_name = "Default";

  int arrow_size = fs * 0.4;
  int arrow_y = mod2_top + mod2_h / 2;

  int label_w = ui.getStringWidth("字体");
  int label_end_x = padding_x + label_w;
  int right_arrow_base_x = right_arrow_center_x - arrow_size / 2;
  int max_w = right_arrow_base_x - label_end_x - fs * 2.0;

  ui.buildFontCache(font_name);
  if (ui.getStringWidth(font_name) > max_w) {
    while (font_name.length() > 3 &&
           ui.getStringWidth(font_name + "...") > max_w) {
      font_name.pop_back();
    }
    font_name += "...";
    ui.buildFontCache(font_name);
  }

  int font_w = ui.getStringWidth(font_name);

  // Right arrow
  int tip_x = right_arrow_center_x + arrow_size / 2;
  int base_x = right_arrow_base_x;
  canvas.fillTriangle(tip_x, arrow_y, base_x, arrow_y - arrow_size, base_x,
                      arrow_y + arrow_size, TFT_BLACK);

  // Font name
  int font_name_right = base_x - fs * 0.5;
  int font_name_x = font_name_right - font_w;
  ui.drawCachedString(font_name, font_name_x, item_y);

  // Left arrow
  int left_arrow_right = font_name_x - fs * 0.5;
  int left_arrow_center_x = left_arrow_right - arrow_size / 2;
  int left_tip_x = left_arrow_center_x - arrow_size / 2;
  int left_base_x = left_arrow_center_x + arrow_size / 2;
  canvas.fillTriangle(left_tip_x, arrow_y, left_base_x, arrow_y - arrow_size,
                      left_base_x, arrow_y + arrow_size, TFT_BLACK);

  // Separator after Module 2
  canvas.drawFastHLine(padding_x, mod2_top + mod2_h, screen_w - 2 * padding_x,
                       TFT_BLACK);

  // Module 3: 锁屏壁纸 (Shared)
  int mod3_top = mod2_top + mod2_h;
  int mod3_h = line_h * 1.8;
  int h_lock = fs * 1.8;

  // Line 3: 启用锁屏壁纸 (Top half of Module 3)
  int line3_center_y = mod3_top + line_h / 2;
  item_y = line3_center_y - fs / 2;
  ui.drawCachedString("启用锁屏壁纸", padding_x, item_y);

  int x_lock = padding_x + ui.getStringWidth("启用锁屏壁纸") + fs * 0.5;
  int w_lock = fs * 2.0;
  int y_lock = line3_center_y - h_lock / 2;

  canvas.drawRoundRect(x_lock, y_lock, w_lock, h_lock, 4, TFT_BLACK);
  bool lock_enabled = ui.isLockScreenEnabled();
  std::string lock_text = lock_enabled ? "开" : "关";
  txt_w = ui.getStringWidth(lock_text);
  ui.drawCachedString(lock_text, x_lock + (w_lock - txt_w) / 2,
                      item_y + fs * 0.2);

  // Line 4: 壁纸文件 (Bottom half of Module 3)
  int line4_center_y = mod3_top + line_h + (line_h * 0.8) / 2;
  item_y = line4_center_y - fs / 2;
  int indent_x = padding_x + fs * 1.0;
  ui.drawCachedString("壁纸", indent_x, item_y);

  std::string path = ui.getLockScreenPath();
  size_t last_slash = path.find_last_of('/');
  std::string filename =
      (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
  if (filename.empty()) filename = "未选择";
  ui.buildFontCache(filename);

  // Truncate if too long
  max_w = screen_w - padding_x - fs * 3.0 -
          (indent_x + ui.getStringWidth("壁纸") + fs * 0.5) - fs;
  if (ui.getStringWidth(filename) > max_w) {
    std::string truncated_filename = "";
    const uint8_t* p = reinterpret_cast<const uint8_t*>(filename.c_str());
    const uint8_t* end = p + filename.size();
    int current_w = 0;
    int dots_w = ui.getStringWidth("...");
    bool truncated = false;
    
    while (p < end) {
      int bytes = 0;
      uint32_t u = Utf8Utils::getNextCharUnicode(p, end, bytes);
      if (bytes == 0) break;
      
      std::string char_str(reinterpret_cast<const char*>(p), bytes);
      int char_w = ui.getStringWidth(char_str);
      
      if (current_w + char_w + dots_w > max_w) {
        truncated = true;
        break;
      }
      
      truncated_filename += char_str;
      current_w += char_w;
      p += bytes;
    }
    if (truncated) {
      truncated_filename += "...";
    }
    filename = truncated_filename;
  }

  int filename_x = indent_x + ui.getStringWidth("壁纸") + fs * 0.5;
  ui.drawCachedString(filename, filename_x, item_y);

  // Select button (Right aligned)
  int x_sel = screen_w - padding_x - fs * 3.0;
  int y_sel = line4_center_y - h_lock / 2;
  canvas.drawRoundRect(x_sel, y_sel, fs * 3.0, h_lock, 4, TFT_BLACK);
  txt_w = ui.getStringWidth("选择");
  ui.drawCachedString("选择", x_sel + (fs * 3.0 - txt_w) / 2,
                      item_y + fs * 0.2);

  // Separator after Module 3
  canvas.drawFastHLine(padding_x, mod3_top + mod3_h, screen_w - 2 * padding_x,
                       TFT_BLACK);

  // Module 4: 清理过期阅读记录
  int mod4_top = mod3_top + mod3_h;
  int mod4_h = line_h;
  item_y = get_centered_y(mod4_top, mod4_h, fs);

  ui.drawCachedString("清理过期阅读记录", padding_x, item_y);

  int x_clean = screen_w - padding_x - fs * 3.0;
  int y_clean = get_centered_y(mod4_top, mod4_h, h_lock);
  canvas.drawRoundRect(x_clean, y_clean, fs * 3.0, h_lock, 4, TFT_BLACK);
  txt_w = ui.getStringWidth("清理");
  ui.drawCachedString("清理", x_clean + (fs * 3.0 - txt_w) / 2,
                      item_y + fs * 0.2);

  // Separator after Module 4
  canvas.drawFastHLine(padding_x, mod4_top + mod4_h, screen_w - 2 * padding_x,
                       TFT_BLACK);

  // Module 5: 使用说明
  int mod5_top = mod4_top + mod4_h;
  int mod5_h = line_h;
  item_y = get_centered_y(mod5_top, mod5_h, fs);

  ui.drawCachedString("使用说明", padding_x, item_y);

  int x_guide = screen_w - padding_x - fs * 3.0;
  int y_guide = get_centered_y(mod5_top, mod5_h, h_lock);
  canvas.drawRoundRect(x_guide, y_guide, fs * 3.0, h_lock, 4, TFT_BLACK);
  txt_w = ui.getStringWidth("阅读");
  ui.drawCachedString("阅读", x_guide + (fs * 3.0 - txt_w) / 2,
                      item_y + fs * 0.2);

  // Bottom Bar
  ui.drawBottomBar({"传书", "书架", "休眠"});
}

State SettingPage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type == MessageType::TOUCH) {
    int x = msg.data.touch.x;
    int y = msg.data.touch.y;

    int screen_h = M5.Display.height();
    int font_size = ui.getFontSize();
    int bottom_bar_h = ui.getFooterHeight();
    int thick_line_y = screen_h - bottom_bar_h;

    // Handle bottom bar clicks
    if (y >= thick_line_y) {
      if (ui.getFontSize() != initial_font_size) {
        deleteIndexFiles();
      }

      int col = x / (M5.Display.width() / 3);
      if (col == 0) {
        return State::TRANSFER;
      } else if (col == 1) {
        return State::BOOKSHELF;
      } else if (col == 2) {
        ui.enterDeepSleep(false);
      }
    }

    // Handle options clicks
    int fs = font_size;
    int padding_x = 20;
    int line_h = fs * 3.2;
    int bar_h = ui.getHeaderHeight();
    int base_y = bar_h - 4;
    int h_lock = fs * 1.8;

    // Module 1: 行间距
    int mod1_top = base_y;
    int mod1_h = line_h;
    int radio_y = mod1_top + mod1_h / 2;
    int r = fs * 0.4;
    int x1 = padding_x + ui.getStringWidth("行间距") + fs * 2.0;
    int x2 = x1 + fs * 5.0;

    if (y >= mod1_top && y <= mod1_top + mod1_h) {
      float old_spacing = current_line_spacing;
      int click_padding_l = std::max(fs, 30);
      int click_padding_r = std::max(fs, 20);
      
      int x1_text_w = ui.getStringWidth("1.5");
      int x1_end = x1 + r + 10 + x1_text_w;
      
      int x2_text_w = ui.getStringWidth("2.0");
      int x2_end = x2 + r + 10 + x2_text_w;

      if (x >= x1 - click_padding_l && x <= x1_end + click_padding_r) {
        current_line_spacing = 1.5f;
      } else if (x >= x2 - click_padding_l && x <= x2_end + click_padding_r) {
        current_line_spacing = 2.0f;
      }

      if (current_line_spacing != old_spacing) {
        ui.setLineSpacingFactor(current_line_spacing);
        ui.saveGlobalConfig();
        deleteIndexFiles();
        ui.drawCurrentPage();  // Redraw
      }
    }

    // Module 2: 字体
    int mod2_top = mod1_top + mod1_h;
    int mod2_h = line_h;
    int arrow_y = mod2_top + mod2_h / 2;
    int arrow_size = fs * 0.4;

    int right_arrow_center_x = M5.Display.width() - padding_x - fs * 1.5;
    int right_arrow_base_x = right_arrow_center_x - arrow_size / 2;

    std::string font_name = ui.getFontName();
    if (font_name.empty()) font_name = "Default";

    int label_w = ui.getStringWidth("字体");
    int label_end_x = padding_x + label_w;
    int max_w = right_arrow_base_x - label_end_x - fs * 2.0;

    ui.buildFontCache(font_name);
    if (ui.getStringWidth(font_name) > max_w) {
      while (font_name.length() > 3 &&
             ui.getStringWidth(font_name + "...") > max_w) {
        font_name.pop_back();
      }
      font_name += "...";
    }

    int font_w = ui.getStringWidth(font_name);
    int font_name_right = right_arrow_base_x - fs * 0.5;
    int font_name_x = font_name_right - font_w;
    int left_arrow_right = font_name_x - fs * 0.5;
    int left_arrow_center_x = left_arrow_right - arrow_size / 2;

    int target_r = std::max(fs, 30);
    if (y >= mod2_top && y <= mod2_top + mod2_h) {
      // Left Arrow
      if (x >= left_arrow_center_x - target_r &&
          x <= left_arrow_center_x + target_r) {
        // Prev font
        if (available_fonts.size() > 1) {
          ui.showPromptBox("加载中...");
          current_font_index =
              (current_font_index - 1 + available_fonts.size()) %
              available_fonts.size();
          ui.setFontPath(available_fonts[current_font_index].c_str());
          ui.saveGlobalConfig();
          ui.clearFontCache();
          ui.drawCurrentPage();  // Redraw
        }
      }
      // Right Arrow
      else if (x >= right_arrow_center_x - target_r &&
               x <= right_arrow_center_x + target_r) {
        // Next font
        if (available_fonts.size() > 1) {
          ui.showPromptBox("加载中...");
          current_font_index =
              (current_font_index + 1) % available_fonts.size();
          ui.setFontPath(available_fonts[current_font_index].c_str());
          ui.saveGlobalConfig();
          ui.clearFontCache();
          ui.drawCurrentPage();  // Redraw
        }
      }
    }

    // Module 3: 锁屏壁纸 (Shared)
    int mod3_top = mod2_top + mod2_h;
    int mod3_h = line_h * 1.8;

    int line3_center_y = mod3_top + line_h / 2;
    int y_lock = line3_center_y - h_lock / 2;
    int x_lock = padding_x + ui.getStringWidth("启用锁屏壁纸") + fs * 0.5;
    int w_lock = fs * 2.0;

    if (x >= x_lock && x <= x_lock + w_lock && y >= y_lock &&
        y <= y_lock + h_lock) {
      ui.setLockScreenEnabled(!ui.isLockScreenEnabled());
      ui.saveGlobalConfig();
      ui.drawCurrentPage();
    }

    int line4_center_y = mod3_top + line_h + (line_h * 0.8) / 2;
    int y_sel = line4_center_y - h_lock / 2;
    int x_sel = M5.Display.width() - padding_x - fs * 3.0;

    if (x >= x_sel && x <= x_sel + fs * 3.0 && y >= y_sel &&
        y <= y_sel + h_lock) {
      return State::IMAGE_PICKER;
    }

    // Module 4: 清理过期阅读记录
    int mod4_top = mod3_top + mod3_h;
    int mod4_h = line_h;
    int y_clean = mod4_top + (mod4_h - h_lock) / 2;
    int x_clean = M5.Display.width() - padding_x - fs * 3.0;

    if (x >= x_clean && x <= x_clean + fs * 3.0 && y >= y_clean &&
        y <= y_clean + h_lock) {
      ui.showPromptBox("清理中...");
      cleanExpiredRecords();
      ui.drawCurrentPage();  // Redraw
    }

    // Module 5: 使用说明
    int mod5_top = mod4_top + mod4_h;
    int mod5_h = line_h;
    int y_guide = mod5_top + (mod5_h - h_lock) / 2;
    int x_guide = M5.Display.width() - padding_x - fs * 3.0;

    if (x >= x_guide && x <= x_guide + fs * 3.0 && y >= y_guide &&
        y <= y_guide + h_lock) {
      return State::USER_GUIDE;
    }
  }
  return State::SETTING;
}

// loadSettings and saveSettings were removed and replaced by BookReader's global config methods.

void SettingPage::deleteIndexFiles() {
  File dir = SD.open("/configs");
  if (!dir) return;
  
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (name.endsWith(".idx")) {
        String path = "/configs/" + name;
        SD.remove(path.c_str());
      }
    }
    entry.close();
  }
  dir.close();
}

void SettingPage::cleanExpiredRecords() {
  File dir = SD.open("/configs");
  if (!dir) return;
  
  std::vector<String> files_to_delete;
  
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (name.endsWith(".cfg") || name.endsWith(".idx")) {
        if (name != "global.cfg" && name != "settings.cfg") {
          String book_filename = name.substring(0, name.length() - 4);
          String book_path = "/books/" + book_filename;
          
          if (!SD.exists(book_path.c_str())) {
            files_to_delete.push_back("/configs/" + name);
          }
        }
      }
    }
    entry.close();
  }
  dir.close();
  
  for (const auto& path : files_to_delete) {
    Serial.print("Deleting expired record: "); Serial.println(path);
    SD.remove(path.c_str());
  }
}
