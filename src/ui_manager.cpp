#include "ui_manager.h"
#include "state_machine.h"
#include "utf8_utils.h"

#include <M5Unified.h>
#include <SD.h>
#include <esp_sleep.h>
#include <cstring>

const int PADDING_X = 20;
const int PADDING_Y = 20;

void UIManager::init() {
  fontLoaded = loadFont();
  bookReader.loadGlobalConfig();
  settingPage.init();
  
  canvas.setPsram(true);
  canvas.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  
  M5.Display.setEpdMode(lgfx::epd_mode_t::epd_text);
}

void UIManager::openBook(const std::string& path, bool show_loading) {
  size_t last_slash = path.find_last_of('/');
  std::string filename = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
  std::string config_path = "/configs/" + filename + ".idx";
  
  bool need_index = !SD.exists(config_path.c_str());
  
  if (show_loading) {
      std::string loading_text;
      if (need_index) {
          loading_text = "正在分页，请稍候...";
      } else {
          int screen_w = canvas.width();
          int max_text_w = screen_w - 80; // Box padding 40, Screen padding 40
          std::string prefix = "正在打开 ";
          std::string display_name = filename;
          
          buildFontCache(prefix + display_name);
          if (getStringWidth(prefix + display_name) > max_text_w) {
              while (!display_name.empty() && 
                     getStringWidth(prefix + display_name + "...") > max_text_w) {
                  do {
                      if (display_name.empty()) break;
                      uint8_t c = display_name.back();
                      display_name.pop_back();
                      if ((c & 0xC0) != 0x80) {
                          break;
                      }
                  } while (true);
              }
              display_name += "...";
          }
          loading_text = prefix + display_name;
      }
      
      showPromptBox(loading_text);
  } else {
      canvas.fillScreen(TFT_WHITE);
      M5.Display.setEpdMode(lgfx::epd_mode_t::epd_fast);
      canvas.pushSprite(&M5.Display, 0, 0);
  }

  if (bookReader.open(path)) {
    pageTurnCount = 0;
    
    // Load config first to get old position
    bookReader.loadConfig(path);
    
    if (need_index || bookReader.isIndexEmpty()) {
        bookReader.buildIndex(pageCache, canvas.width() - 2 * PADDING_X, canvas.height() - 2 * PADDING_Y);
        bookReader.saveIndex();
        bookReader.saveConfig(); // Save the updated record!
    }
    
    saveGlobalConfig(path);
    
    bookReader.readCurrentPage(pageCache, canvas.width() - 2 * PADDING_X, canvas.height() - 2 * PADDING_Y);
    drawReadingView();
  }
}

void UIManager::nextPage() {
  pageTurnCount++;
  bookReader.nextPage(pageCache, canvas.width() - 2 * PADDING_X, canvas.height() - 2 * PADDING_Y);
  drawReadingView();
}

void UIManager::prevPage() {
  pageTurnCount++;
  bookReader.prevPage(pageCache, canvas.width() - 2 * PADDING_X, canvas.height() - 2 * PADDING_Y);
  drawReadingView();
}

class FileWrapper : public lgfx::v1::DataWrapper {
 private:
  fs::File _file;
 public:
  FileWrapper(fs::File file) : _file(file) {
    need_transaction = true;
  }
  int read(uint8_t *buf, uint32_t len) override {
    return _file.read(buf, len);
  }
  void skip(int32_t offset) override {
    _file.seek(_file.position() + offset);
  }
  bool seek(uint32_t offset) override {
    return _file.seek(offset);
  }
  void close(void) override {
    _file.close();
  }
  int32_t tell(void) override {
    return _file.position();
  }
};

bool UIManager::loadFont() {
  fs::File file = SD.open("/font.ttf");
  if (!file) {
    Serial.println("Failed to open /font.ttf from SD");
    return false;
  }

  FileWrapper wrapper(file);
  if (M5.Display.loadFont(&wrapper)) {
    Serial.println("Font loaded successfully via wrapper.");
    return true;
  }
  Serial.println("Failed to load font via wrapper.");
  return false;
}



void UIManager::handleInput() {
  // Handle Wheel
  if (M5.BtnA.wasClicked()) {
    SystemMessage_t msg;
    msg.type = MessageType::WHEEL;
    msg.timestamp = millis();
    msg.data.wheel.delta = -1;
    send_message(msg);
  }
  if (M5.BtnC.wasClicked()) {
    SystemMessage_t msg;
    msg.type = MessageType::WHEEL;
    msg.timestamp = millis();
    msg.data.wheel.delta = 1;
    send_message(msg);
  }
  if (M5.BtnB.wasClicked()) {
    SystemMessage_t msg;
    msg.type = MessageType::SYSTEM;
    msg.timestamp = millis();
    msg.data.sys_code = 1; // Press
    send_message(msg);
  }

  // Handle Touch
  auto detail = M5.Touch.getDetail();
  if (detail.wasClicked()) {
    SystemMessage_t msg;
    msg.type = MessageType::TOUCH;
    msg.timestamp = millis();
    msg.data.touch.x = detail.x;
    msg.data.touch.y = detail.y;
    send_message(msg);
  }
}


void UIManager::drawReadingView(bool forceFullRefresh) {
  canvas.fillScreen(TFT_WHITE);
  
  std::string text = bookReader.getPageText();
  if (text.empty()) {
    buildFontCache("文件为空或读取失败");
    drawCachedString("文件为空或读取失败", PADDING_X, PADDING_Y);
  } else {
    buildFontCache(text);
    drawCachedString(text, PADDING_X, PADDING_Y);
  }
  
  if (forceFullRefresh || pageTurnCount % 5 == 0) {
    M5.Display.setEpdMode(lgfx::epd_mode_t::epd_quality);
    Serial.println("Full refresh triggered.");
    pageTurnCount = 0;
  } else {
    M5.Display.setEpdMode(lgfx::epd_mode_t::epd_text);
  }
  
  canvas.pushSprite(&M5.Display, 0, 0);
}

void UIManager::drawReaderMenu() {
  pageTurnCount++; // Increment shared counter
  
  if (pageTurnCount % 5 == 0) {
    M5.Display.setEpdMode(lgfx::epd_mode_t::epd_quality);
    Serial.println("Reader menu full refresh triggered.");
  } else {
    M5.Display.setEpdMode(lgfx::epd_mode_t::epd_text);
  }

  int screen_w = canvas.width();
  int screen_h = canvas.height();
  int font_size = pageCache.getFontSize();
  int row_h = font_size * 2.5;
  int bar_h = row_h * 2;
  int text_y_offset = (row_h - font_size) / 2;
  int col_w = screen_w / 2;

  // Draw Top Bar
  canvas.fillRect(0, 0, screen_w, bar_h, TFT_WHITE);
  canvas.fillRect(0, bar_h - 4, screen_w, 4, TFT_BLACK); // Thick line
  
  // Top Left: "返回" (Row 1)
  std::string back_text = "返回";
  buildFontCache(back_text);
  int back_w = getStringWidth(back_text);
  drawCachedString(back_text, col_w / 2 - back_w / 2, text_y_offset);
  
  // Draw vertical separator for "返回" button (only in Row 1)
  canvas.drawLine(col_w, 0, col_w, row_h, TFT_BLACK);

  // Top Right: Battery (Row 1)
  drawBattery(canvas, canvas.width() - 20, text_y_offset);

  // Draw separator line between Row 1 and Row 2
  canvas.drawLine(0, row_h, screen_w, row_h, TFT_BLACK);

  // Row 2: Filename
  std::string book_path = bookReader.getBookPath();
  size_t last_slash = book_path.find_last_of('/');
  std::string filename = (last_slash == std::string::npos) ? book_path : book_path.substr(last_slash + 1);
  
  // Truncate filename if too long
  int max_name_w = screen_w - 40;
  buildFontCache(filename);
  if (getStringWidth(filename) > max_name_w) {
      while (!filename.empty() && 
             getStringWidth(filename + "...") > max_name_w) {
          do {
              if (filename.empty()) break;
              uint8_t c = filename.back();
              filename.pop_back();
              if ((c & 0xC0) != 0x80) {
                  break;
              }
          } while (true);
      }
      filename += "...";
      buildFontCache(filename);
  }
  
  int filename_w = getStringWidth(filename);
  int filename_x = screen_w / 2 - filename_w / 2;
  int text_y_offset2 = row_h + (row_h - font_size) / 2;
  drawCachedString(filename, filename_x, text_y_offset2);

  // Draw Bottom Bar
  bool is_user_guide = isReadingUserGuide();
  int bottom_h = is_user_guide ? row_h : row_h * 2;
  int bottom_y = screen_h - bottom_h;
  canvas.fillRect(0, bottom_y, screen_w, bottom_h, TFT_WHITE);
  canvas.fillRect(0, bottom_y, screen_w, 4, TFT_BLACK); // Thick line at top of bar

  if (is_user_guide) {
      // Draw progress centered in the 100px bar
      int current_page = bookReader.getCurrentPageIndex() + 1;
      int total_pages = bookReader.getTotalPages();
      float progress = bookReader.getProgress();
      
      char prog_str[32];
      snprintf(prog_str, sizeof(prog_str), "%d/%d  %.1f%%", current_page, total_pages, progress);
      std::string prog_text = prog_str;
      buildFontCache(prog_text);
      int prog_w = getStringWidth(prog_text);
      drawCachedString(prog_text, (screen_w - prog_w) / 2, bottom_y + (bottom_h - font_size) / 2);
  } else {
      // Row 1: Progress
      int current_page = bookReader.getCurrentPageIndex() + 1;
      int total_pages = bookReader.getTotalPages();
      float progress = bookReader.getProgress();
      
      char prog_str[32];
      snprintf(prog_str, sizeof(prog_str), "%d/%d  %.1f%%", current_page, total_pages, progress);
      std::string prog_text = prog_str;
      buildFontCache(prog_text);
      int prog_w = getStringWidth(prog_text);
      drawCachedString(prog_text, (screen_w - prog_w) / 2, bottom_y + (row_h - font_size) / 2);

      // Draw line separating the two rows
      canvas.drawLine(0, bottom_y + row_h, screen_w, bottom_y + row_h, TFT_BLACK);

      // Row 2: Buttons
      int row2_y = bottom_y + row_h;

      // Bottom Left: "跳转"
      std::string jump_text = "跳转";
      buildFontCache(jump_text);
      int jump_w = getStringWidth(jump_text);
      drawCachedString(jump_text, col_w / 2 - jump_w / 2, row2_y + (row_h - font_size) / 2);

      // Bottom Right: "休眠"
      std::string sleep_text = "休眠";
      buildFontCache(sleep_text);
      int sleep_w = getStringWidth(sleep_text);
      drawCachedString(sleep_text, col_w + col_w / 2 - sleep_w / 2, row2_y + (row_h - font_size) / 2);

      // Draw vertical separator in row 2
      canvas.drawLine(col_w, row2_y, col_w, screen_h, TFT_BLACK);
  }

  canvas.pushSprite(&M5.Display, 0, 0);
}

void UIManager::drawReaderJumpMenu(const std::string& input) {
  pageTurnCount++;
  if (pageTurnCount % 5 == 0) {
    M5.Display.setEpdMode(lgfx::epd_mode_t::epd_quality);
    Serial.println("Jump menu full refresh triggered.");
    pageTurnCount = 0;
  } else {
    M5.Display.setEpdMode(lgfx::epd_mode_t::epd_fast);
  }

  int screen_w = canvas.width();
  int screen_h = canvas.height();
  
  int font_size = pageCache.getFontSize();
  int box_w = static_cast<int>(font_size * 9.44);
  int box_h = static_cast<int>(font_size * 11.11);
  int box_x = (screen_w - box_w) / 2;
  int box_y = (screen_h - box_h) / 2;

  int input_box_h = font_size * 2;
  int input_box_y = box_y - input_box_h - 10;
  int total_text_y = input_box_y - font_size - 5;
  int btn_y = box_y + box_h + 10;
  int btn_h = font_size * 2;
  
  int modal_min_y = total_text_y - 10;
  int modal_max_y = btn_y + btn_h + 10;
  
  // Fill entire modal background to prevent bleeding
  canvas.fillRect(box_x - 10, modal_min_y, box_w + 20, modal_max_y - modal_min_y, TFT_WHITE);
  canvas.drawRect(box_x - 10, modal_min_y, box_w + 20, modal_max_y - modal_min_y, TFT_BLACK);

  canvas.fillRect(box_x, box_y, box_w, box_h, TFT_WHITE);
  canvas.drawRect(box_x, box_y, box_w, box_h, TFT_BLACK);

  for (int i = 1; i < 3; ++i) {
      canvas.drawLine(box_x + i * (box_w / 3), box_y, box_x + i * (box_w / 3), box_y + box_h, TFT_BLACK);
  }
  for (int i = 1; i < 4; ++i) {
      canvas.drawLine(box_x, box_y + i * (box_h / 4), box_x + box_w, box_y + i * (box_h / 4), TFT_BLACK);
  }


  std::vector<std::string> labels = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "返回", "0", "删除"};
  
  for (int i = 0; i < 12; ++i) {
      int col = i % 3;
      int row = i / 3;
      int cell_x = box_x + col * (box_w / 3);
      int cell_y = box_y + row * (box_h / 4);
      
      std::string label = labels[i];
      buildFontCache(label);
      int txt_w = getStringWidth(label);
      int text_x = cell_x + (box_w / 3 - txt_w) / 2;
      int text_y = cell_y + (box_h / 4 - font_size) / 2;
      drawCachedString(label, text_x, text_y);
  }


  canvas.fillRect(box_x, input_box_y, box_w, input_box_h, TFT_WHITE);
  canvas.drawRect(box_x, input_box_y, box_w, input_box_h, TFT_BLACK);
  
  buildFontCache(input);
  int txt_w = getStringWidth(input);
  int text_x = box_x + 10;
  int text_y = input_box_y + (input_box_h - font_size) / 2;
  drawCachedString(input, text_x, text_y);

  // Draw total pages
  int total_pages = bookReader.getTotalPages();
  std::string total_text = "共 " + std::to_string(total_pages) + " 页";
  buildFontCache(total_text);
  int total_txt_w = getStringWidth(total_text);
  int total_text_x = box_x + box_w - total_txt_w - 10;
  drawCachedString(total_text, total_text_x, total_text_y);


  int btn_w = (box_w - 20) / 2;
  
  int btn1_x = box_x;
  int btn2_x = box_x + box_w - btn_w;
  
  canvas.fillRect(btn1_x, btn_y, btn_w, btn_h, TFT_WHITE);
  canvas.drawRect(btn1_x, btn_y, btn_w, btn_h, TFT_BLACK);
  std::string btn1_label = "按页码";
  buildFontCache(btn1_label);
  txt_w = getStringWidth(btn1_label);
  drawCachedString(btn1_label, btn1_x + (btn_w - txt_w) / 2, btn_y + (btn_h - font_size) / 2);

  canvas.fillRect(btn2_x, btn_y, btn_w, btn_h, TFT_WHITE);
  canvas.drawRect(btn2_x, btn_y, btn_w, btn_h, TFT_BLACK);
  std::string btn2_label = "按百分比";
  buildFontCache(btn2_label);
  txt_w = getStringWidth(btn2_label);
  drawCachedString(btn2_label, btn2_x + (btn_w - txt_w) / 2, btn_y + (btn_h - font_size) / 2);

  canvas.pushSprite(&M5.Display, 0, 0);
}

bool UIManager::jumpToPage(int page) {
    int screen_w = canvas.width();
    int screen_h = canvas.height();
    return bookReader.jumpToPage(page, pageCache, screen_w - 2 * PADDING_X, screen_h - 2 * PADDING_Y);
}

bool UIManager::jumpToPercentage(float percentage) {
    int screen_w = canvas.width();
    int screen_h = canvas.height();
    return bookReader.jumpToPercentage(percentage, pageCache, screen_w - 2 * PADDING_X, screen_h - 2 * PADDING_Y);
}

void UIManager::drawCachedString(const std::string& text, int x, int y) {
  int cursor_x = x;
  int cursor_y = y;
  
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
  const uint8_t* end = p + text.size();

  while (p < end)
  {
    int bytes = 0;
    uint32_t unicode = Utf8Utils::getNextCharUnicode(p, end, bytes);
    
    if (bytes > 0)
    {
      p += bytes;
      if (unicode <= 0xFFFF && unicode > 0)
      {
        if (unicode == '\n') {
          cursor_x = x;
          cursor_y += pageCache.getFontSize() * bookReader.getLineSpacingFactor(); // Line height
          continue;
        }
        if (unicode == '\r') {
          continue;
        }
        
        uint16_t u = static_cast<uint16_t>(unicode);
        const CharGlyphInfo* info = pageCache.getCharGlyphInfo(u);
        int char_w = info ? info->width : (u == ' ' ? pageCache.getFontSize() / 2 : pageCache.getFontSize());
        
        if (info) {
          const uint8_t* bitmap = pageCache.getCharBitmap(u);
          if (bitmap) {
            void* canvasBuffer = canvas.getBuffer();
            if (canvasBuffer) {
              uint8_t* pDst = static_cast<uint8_t*>(canvasBuffer);
              const uint8_t* pSrc = bitmap;
              int canvasWidth = canvas.width();
              int canvasHeight = canvas.height();
              
              for (int h = 0; h < info->bitmapH; ++h) {
                int dst_y = cursor_y + info->y_offset + h;
                int dst_x = cursor_x + info->x_offset;
                
                // Clipping
                if (dst_y >= 0 && dst_y < canvasHeight) {
                  int copyWidth = info->bitmapW;
                  int src_offset = 0;
                  
                  if (dst_x < 0) {
                    src_offset = -dst_x;
                    copyWidth += dst_x;
                    dst_x = 0;
                  }
                  if (dst_x + copyWidth > canvasWidth) {
                    copyWidth = canvasWidth - dst_x;
                  }
                  
                  if (copyWidth > 0) {
                    memcpy(pDst + dst_y * canvasWidth + dst_x, 
                           pSrc + h * info->bitmapW + src_offset, 
                           copyWidth);
                  }
                }
              }
            }
          }
        }
        cursor_x += char_w;
      }
    }
    else
    {
      p++;
    }
  }
}





int UIManager::getStringWidth(const std::string& text) {
    int width = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
    const uint8_t* end = p + text.size();
    while (p < end) {
        int bytes = 0;
        uint32_t unicode = Utf8Utils::getNextCharUnicode(p, end, bytes);
        
        if (bytes == 0) break;
        const CharGlyphInfo* info = pageCache.getCharGlyphInfo(unicode);
        width += info ? info->width : pageCache.getFontSize();
        p += bytes;
    }
    return width;
}

std::string UIManager::getLastReadBook() {
    if (!SD.exists("/configs/global.cfg")) {
        return "";
    }
    File file = SD.open("/configs/global.cfg", FILE_READ);
    if (!file) {
        return "";
    }
    String path = file.readStringUntil('\n');
    path.trim();
    file.close();
    return path.c_str();
}

void UIManager::saveGlobalConfig(const std::string& book_path) {
    if (SD.exists("/configs/global.cfg")) {
        SD.remove("/configs/global.cfg");
    }
    File file = SD.open("/configs/global.cfg", FILE_WRITE);
    if (file) {
        file.println(book_path.c_str());
        file.close();
    }
}

void UIManager::showPromptBox(const std::string& text) {
  buildFontCache(text);
  
  int font_size = pageCache.getFontSize();
  int screen_w = canvas.width();
  int screen_h = canvas.height();
  
  int text_w = getStringWidth(text);
  int box_w = text_w + 40;
  int box_h = font_size + 40;
  int box_x = (screen_w - box_w) / 2;
  int box_y = (screen_h - box_h) / 2;

  canvas.fillRect(box_x, box_y, box_w, box_h, TFT_WHITE);
  canvas.drawRect(box_x, box_y, box_w, box_h, TFT_BLACK);
  
  int text_x = box_x + (box_w - text_w) / 2;
  int text_y = box_y + (box_h - font_size) / 2;
  
  drawCachedString(text, text_x, text_y);
  
  M5.Display.setEpdMode(lgfx::epd_mode_t::epd_fast);
  canvas.pushSprite(&M5.Display, 0, 0);
}

void UIManager::saveCurrentBookConfig() {
    bookReader.saveConfig();
}

void UIManager::setBootToReaderFlag(bool enable) {
    const char* flag_path = "/configs/boot_to_reader.flag";
    if (enable) {
        File file = SD.open(flag_path, FILE_WRITE);
        if (file) {
            file.println("1");
            file.close();
        }
    } else {
        if (SD.exists(flag_path)) {
            SD.remove(flag_path);
        }
    }
}

bool UIManager::checkBootToReaderFlag() {
    return SD.exists("/configs/boot_to_reader.flag");
}

void UIManager::enterDeepSleep(bool from_reader) {
  canvas.fillScreen(TFT_WHITE);
  
  bool lock_enabled = bookReader.isLockScreenEnabled();
  std::string lock_path = bookReader.getLockScreenPath();
  bool drawn = false;
  
  if (lock_enabled && !lock_path.empty() && SD.exists(lock_path.c_str())) {
      File f = SD.open(lock_path.c_str(), FILE_READ);
      if (f) {
          MyFileDataWrapper wrapper(f);
          if (lock_path.find(".png") != std::string::npos || lock_path.find(".PNG") != std::string::npos) {
              drawn = canvas.drawPng(&wrapper, 0, 0);
          } else {
              drawn = canvas.drawJpg(&wrapper, 0, 0);
          }
          f.close();
      }
  }
  
  if (!drawn) {
      // 1. Build Cache for Chinese sleep prompts
      std::string sleep_chars = "系统已进入休眠状态"
                                "向内长按滚轮键3秒即可唤醒";
      buildFontCache(sleep_chars);
      
      int font_size = getFontSize();
      int screen_w = canvas.width();
      int screen_h = canvas.height();
      
      // 2. Draw Prompts
      std::string prompt1 = "系统已进入休眠";
      int txt_w = getStringWidth(prompt1);
      int prompt1_y = screen_h / 2 - font_size;
      drawCachedString(prompt1, screen_w / 2 - txt_w / 2, prompt1_y);
      
      std::string prompt2 = "向内长按滚轮键3秒即可唤醒";
      txt_w = getStringWidth(prompt2);
      int prompt2_y = prompt1_y + 2 * font_size;
      drawCachedString(prompt2, screen_w / 2 - txt_w / 2, prompt2_y);
  }
  
  // 4. Push Sprite with high quality mode to clear ghosting
  M5.Display.setEpdMode(lgfx::epd_mode_t::epd_quality);
  canvas.pushSprite(&M5.Display, 0, 0);
  
  // 5. Setup sleep hardware
  esp_sleep_enable_ext0_wakeup((gpio_num_t)38, 0);
  setBootToReaderFlag(from_reader);
  if (from_reader) {
      saveCurrentBookConfig();
  }
  
  delay(500);
  M5.Power.deepSleep(0, false);
}

void UIManager::setPage(Page* page) {
  if (currentPage) {
    currentPage->exit();
  }
  currentPage = page;
  if (currentPage) {
    currentPage->enter();
  }
}

void UIManager::drawCurrentPage() {
  if (currentPage) {
    currentPage->draw(canvas);
    
    if (checkUiFullRefresh()) {
      M5.Display.setEpdMode(lgfx::epd_mode_t::epd_quality);
      Serial.println("UI full refresh triggered.");
    } else {
      M5.Display.setEpdMode(lgfx::epd_mode_t::epd_text);
    }
    
    canvas.pushSprite(&M5.Display, 0, 0);
  }
}

void UIManager::setPageByState(State state) {
  switch (state) {
    case State::BOOKSHELF:
      setPage(&bookshelfPage);
      break;
    case State::SETTING:
      setPage(&settingPage);
      break;
    case State::TRANSFER:
      setPage(&transferPage);
      break;
    case State::USER_GUIDE:
      setPage(&userGuidePage);
      break;
    case State::IMAGE_PICKER:
      setPage(&imagePickerPage);
      break;
    default:
      setPage(nullptr);
      break;
  }
}

void UIManager::drawBottomBar(const std::vector<std::string>& items, int selected_idx) {
  int font_size = pageCache.getFontSize();
  int screen_w = canvas.width();
  int screen_h = canvas.height();
  int bottom_bar_h = getFooterHeight();
  int thick_line_y = screen_h - bottom_bar_h;
  
  canvas.fillRect(0, thick_line_y, screen_w, 4, TFT_BLACK);
  
  int bar_h = bottom_bar_h;
  int text_y = thick_line_y + (bar_h - font_size) / 2;
  int num_items = items.size();
  if (num_items == 0) return;
  
  int col_w = screen_w / num_items;
  
  for (int i = 0; i < num_items; ++i) {
    int txt_w = getStringWidth(items[i]);
    drawCachedString(items[i], i * col_w + col_w / 2 - txt_w / 2, text_y);
    
    if (i > 0) {
      canvas.drawLine(i * col_w, thick_line_y, i * col_w, screen_h, TFT_BLACK);
    }
  }
}

void UIManager::drawBattery(LGFX_Sprite& canvas, int right_x, int y) {
  int battery = M5.Power.getBatteryLevel();
  char batt_str[16];
  snprintf(batt_str, sizeof(batt_str), "%d%%", battery);
  
  int font_size = pageCache.getFontSize();
  buildFontCache(batt_str);
  int batt_txt_w = getStringWidth(batt_str);
  
  int icon_h = font_size * 0.75;
  int icon_w = icon_h * 2.1;
  int cap_h = icon_h * 0.4;
  int cap_w = icon_h * 0.15;
  
  int total_w = batt_txt_w + 10 + icon_w + cap_w;
  int start_x = right_x - total_w;
  
  drawCachedString(batt_str, start_x, y);
  
  int icon_x = start_x + batt_txt_w + 10;
  int icon_y = y + (font_size - icon_h) / 2;
  
  canvas.drawRect(icon_x, icon_y, icon_w, icon_h, TFT_BLACK);
  canvas.fillRect(icon_x + icon_w, icon_y + (icon_h - cap_h) / 2, cap_w, cap_h, TFT_BLACK);
  
  int inner_w = icon_w - 4;
  int inner_h = icon_h - 4;
  int fill_w = (battery * inner_w) / 100;
  
  if (fill_w > 0) {
      canvas.fillRect(icon_x + 2, icon_y + 2, fill_w, inner_h, TFT_BLACK);
  }
}
