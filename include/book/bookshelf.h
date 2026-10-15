#pragma once

#include <Arduino.h>
#include <vector>

struct BookInfo {
  String filename;
  uint32_t size;
  uint32_t pos;
  int progress;
  bool isLastRead = false;
};

class Bookshelf {
 public:
  void init();
  const std::vector<BookInfo>& getBookList();
  void refresh();

 private:
  std::vector<BookInfo> bookList;
};
