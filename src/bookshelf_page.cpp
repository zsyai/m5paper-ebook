#include "bookshelf_page.h"
#include "ui_manager.h"

#include <M5Unified.h>
#include <SD.h>

extern UIManager ui;

void BookshelfPage::init() {
}

void BookshelfPage::enter() {
  bookshelf.refresh();
}

void BookshelfPage::exit() {
}

void BookshelfPage::draw(LGFX_Sprite& canvas) {
  const std::vector<BookInfo>& books = bookshelf.getBookList();

  // 1. Build Cache
  std::string all_text = "我的书架剩余电量已读完未读上次阅读上一页下一页滚轮下滑显示下一页";
  all_text += "传书设置休眠·%0123456789.MBKB |";
  for (const auto& book : books) {
    all_text += book.filename.c_str();
  }
  ui.buildFontCache(all_text);

  canvas.fillScreen(TFT_WHITE);
  
  int font_size = ui.getFontSize();
  int screen_w = canvas.width();
  int screen_h = canvas.height();
  
  // 2. Draw Header
  int bar_h = ui.getHeaderHeight();
  int text_y = (bar_h - font_size) / 2;
  
  ui.drawCachedString("我的书架", 20, text_y);
  
  ui.drawBattery(canvas, canvas.width() - 20, text_y);
  
  // Draw header separator line
  int line_y = bar_h - 4;
  canvas.fillRect(0, line_y, screen_w, 4, TFT_BLACK);
  
  // 3. Draw Book List
  int item_y = line_y + 10;
  int item_h = font_size * 3 + 40;
  int items_per_page = getItemsPerPage();
  int bottom_y = screen_h - ui.getFooterHeight() - font_size * 1.5f;
  
  fileBrowserItemYPositions.clear();
  
  int start_idx = bookshelfPage * items_per_page;
  int end_idx = std::min((int)books.size(), start_idx + items_per_page);
  
  for (int i = start_idx; i < end_idx; ++i) {
    fileBrowserItemYPositions.push_back(item_y);
    
    String book_filename = books[i].filename;
    uint32_t size = books[i].size;
    uint32_t pos = books[i].pos;
    int progress = books[i].progress;
    
    // Draw book name (Line 1)
    std::string display_name = book_filename.c_str();
    int max_name_w = screen_w - 60;
    if (ui.getStringWidth(display_name) > max_name_w) {
        while (!display_name.empty() && 
               ui.getStringWidth(display_name + "...") > max_name_w) {
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
    
    ui.drawCachedString(display_name, 20, item_y + 20);
    
    // Draw progress and size (Line 2)
    char info_str[64];
    if (progress >= 100) {
        if (books[i].isLastRead) {
            snprintf(info_str, sizeof(info_str), "上次读完 · %.1fMB", 
                     size / (1024.0 * 1024.0));
        } else {
            snprintf(info_str, sizeof(info_str), "已读完 · %.1fMB", 
                     size / (1024.0 * 1024.0));
        }
    } else if (books[i].isLastRead) {
        snprintf(info_str, sizeof(info_str), "上次阅读%d%% · %.1fMB", 
                 progress, size / (1024.0 * 1024.0));
    } else if (pos > 0) {
        snprintf(info_str, sizeof(info_str), "已读%d%% · %.1fMB", 
                 progress, size / (1024.0 * 1024.0));
    } else {
        snprintf(info_str, sizeof(info_str), "未读 · %.1fMB", 
                 size / (1024.0 * 1024.0));
    }
    if (size < 1024 * 1024) {
        if (progress >= 100) {
            if (books[i].isLastRead) {
                snprintf(info_str, sizeof(info_str), "上次读完 · %dKB", 
                         size / 1024);
            } else {
                snprintf(info_str, sizeof(info_str), "已读完 · %dKB", 
                         size / 1024);
            }
        } else if (books[i].isLastRead) {
            snprintf(info_str, sizeof(info_str), "上次阅读%d%% · %dKB", 
                     progress, size / 1024);
        } else if (pos > 0) {
            snprintf(info_str, sizeof(info_str), "已读%d%% · %dKB", 
                     progress, size / 1024);
        } else {
            snprintf(info_str, sizeof(info_str), "未读 · %dKB", size / 1024);
        }
    }
    
    ui.drawCachedString(info_str, 40, item_y + 20 + font_size * 1.5f);
    
    item_y += item_h;
    
    // Draw solid line across screen
    canvas.drawFastHLine(0, item_y - 5, screen_w, TFT_BLACK);
  }
  
  if (books.empty()) {
    ui.drawCachedString("未找到书籍。", 20, item_y);
  }
  
  // 4. Draw Bottom
  int btn_y = bottom_y;
  
  if (bookshelfPage > 0) {
      int txt_w = ui.getStringWidth("上一页");
      ui.drawCachedString("上一页", screen_w / 4 - txt_w / 2, btn_y);
  }
  
  if (end_idx < books.size()) {
      std::string next_page_text = "下一页";
      int txt_w = ui.getStringWidth(next_page_text);
      ui.drawCachedString(next_page_text, screen_w * 3 / 4 - txt_w / 2, btn_y);
  }
  
  int thick_line_y = btn_y + font_size * 1.5;
  canvas.fillRect(0, thick_line_y, screen_w, 4, TFT_BLACK);
  
  bar_h = screen_h - thick_line_y;
  text_y = thick_line_y + (bar_h - font_size) / 2;
  int col_w = screen_w / 3;
  
  int txt_w = ui.getStringWidth("传书");
  ui.drawCachedString("传书", col_w / 2 - txt_w / 2, text_y);
  
  txt_w = ui.getStringWidth("设置");
  ui.drawCachedString("设置", col_w + col_w / 2 - txt_w / 2, text_y);
  
  txt_w = ui.getStringWidth("休眠");
  ui.drawCachedString("休眠", 2 * col_w + col_w / 2 - txt_w / 2, text_y);
  
  canvas.drawLine(col_w, thick_line_y, col_w, screen_h, TFT_BLACK);
  canvas.drawLine(2 * col_w, thick_line_y, 2 * col_w, screen_h, TFT_BLACK);

  if (ui.isUsingBuiltinFont()) {
    std::string line1 = "字体文件缺失，正在使用内置字体";
    std::string line2 = "请通过“传书”上传字体文件";
    ui.buildFontCache(line1 + line2);

    int font_size = ui.getFontSize();
    int screen_w = canvas.width();
    int screen_h = canvas.height();

    int w1 = ui.getStringWidth(line1);
    int w2 = ui.getStringWidth(line2);
    int box_w = std::max(w1, w2) + 40;
    int box_h = font_size * 2 + 60;  // 2 lines + padding
    int box_x = (screen_w - box_w) / 2;
    int box_y = (screen_h - box_h) / 2;

    canvas.fillRect(box_x, box_y, box_w, box_h, TFT_WHITE);
    canvas.drawRect(box_x, box_y, box_w, box_h, TFT_BLACK);

    int text_x1 = box_x + (box_w - w1) / 2;
    int text_x2 = box_x + (box_w - w2) / 2;
    int text_y1 = box_y + 20;
    int text_y2 = text_y1 + font_size + 20;

    ui.drawCachedString(line1, text_x1, text_y1);
    ui.drawCachedString(line2, text_x2, text_y2);
  }
}

State BookshelfPage::handleEvent(const SystemMessage_t& msg) {
  const std::vector<BookInfo>& books = bookshelf.getBookList();

  if (msg.type == MessageType::TOUCH) {
    int x = msg.data.touch.x;
    int y = msg.data.touch.y;

    int font_size = ui.getFontSize();
    int screen_h = M5.Display.height();
    int bottom_y = screen_h - ui.getFooterHeight() - font_size * 1.5f;
    int items_per_page = getItemsPerPage();

    int thick_line_y = bottom_y + font_size * 1.5f;

    if (y >= bottom_y && y < thick_line_y) {
      bool changed = false;
      if (x < M5.Display.width() / 2) {
        if (bookshelfPage > 0) {
          prevBookshelfPage();
          changed = true;
        }
      } else {
        if ((bookshelfPage + 1) * items_per_page < books.size()) {
          nextBookshelfPage();
          changed = true;
        }
      }
      if (changed) {
        ui.drawCurrentPage(); // Trigger redraw on page turn
      }
      return State::BOOKSHELF;
    } else if (y >= thick_line_y) {
      int col = x / (M5.Display.width() / 3);
      if (col == 0) {
        return State::TRANSFER;
      } else if (col == 1) {
        return State::SETTING;
      } else if (col == 2) {
        ui.enterDeepSleep(false);
      }
    } else if (y >= 40 && y < bottom_y) {
      int clicked_item = getFileBrowserItemAt(y);
      if (clicked_item >= 0 && (size_t)clicked_item < books.size()) {
        String book_path = "/books/" + books[clicked_item].filename;
        ui.openBook(book_path.c_str());
        return State::READER;
      }
    }
  }
  return State::BOOKSHELF;
}

int BookshelfPage::getFileBrowserItemAt(int y) const {
    int font_size = ui.getFontSize();
    int item_h = font_size * 3 + 40;
    int items_per_page = getItemsPerPage();
    
    for (size_t i = 0; i < fileBrowserItemYPositions.size(); ++i) {
        int start_y = fileBrowserItemYPositions[i];
        int end_y = start_y + item_h;
        if (y >= start_y && y < end_y) {
            return bookshelfPage * items_per_page + i;
        }
    }
    return -1;
}

void BookshelfPage::nextBookshelfPage() {
    const std::vector<BookInfo>& books = bookshelf.getBookList();
    int items_per_page = getItemsPerPage();
    
    if ((bookshelfPage + 1) * items_per_page < books.size()) {
        bookshelfPage++;
    }
}

void BookshelfPage::prevBookshelfPage() {
    if (bookshelfPage > 0) {
        bookshelfPage--;
    }
}

int BookshelfPage::getItemsPerPage() const {
    int font_size = ui.getFontSize();
    int item_h = font_size * 3 + 40;
    int screen_h = M5.Display.height();
    int bar_h = ui.getHeaderHeight();
    int item_y = bar_h - 4 + 10;
    int bottom_y = screen_h - ui.getFooterHeight() - font_size * 1.5f;
    int available_h = bottom_y - item_y;
    int items_per_page = available_h / item_h;
    return (items_per_page <= 0) ? 1 : items_per_page;
}
