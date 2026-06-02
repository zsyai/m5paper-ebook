#include "bookshelf.h"

#include <SD.h>

void Bookshelf::init() {
  refresh();
}

void Bookshelf::refresh() {
  bookList.clear();
  
  String last_read_filename = "";
  File cfg = SD.open("/configs/global.cfg");
  if (cfg) {
    String path = cfg.readStringUntil('\n');
    path.trim();
    cfg.close();
    int last_slash = path.lastIndexOf('/');
    if (last_slash != -1) {
      last_read_filename = path.substring(last_slash + 1);
    } else {
      last_read_filename = path;
    }
  }

  File root = SD.open("/books");
  if (!root) {
    Serial.println("Failed to open /books");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("/books is not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String filename = file.name();
      if (filename.endsWith(".txt") || filename.endsWith(".TXT")) {
        BookInfo info;
        info.filename = filename;
        info.size = file.size();
        
        // Load progress
        info.pos = 0;
        String config_path = "/configs/" + filename + ".cfg";
        File cfg_file = SD.open(config_path.c_str());
        bool completed = false;
        if (cfg_file) {
          while (cfg_file.available()) {
            String line = cfg_file.readStringUntil('\n');
            line.trim();
            if (line.startsWith("pos=")) {
              info.pos = line.substring(4).toInt();
            } else if (line.startsWith("completed=")) {
              completed = line.substring(10).toInt() == 1;
            }
          }
          cfg_file.close();
        }
        
        if (completed) {
          info.progress = 100;
        } else if (info.size > 0) {
          info.progress = (info.pos * 100) / info.size;
          if (info.progress >= 100) {
            info.progress = 99;
          }
        } else {
          info.progress = 0;
        }
        
        info.isLastRead = (filename == last_read_filename);
        
        bookList.push_back(info);
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();

  // Move last read book to the front
  for (auto it = bookList.begin(); it != bookList.end(); ++it) {
    if (it->isLastRead) {
      BookInfo last_read = *it;
      bookList.erase(it);
      bookList.insert(bookList.begin(), last_read);
      break;
    }
  }
}

const std::vector<BookInfo>& Bookshelf::getBookList() {
  return bookList;
}
