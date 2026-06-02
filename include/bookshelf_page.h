#pragma once

#include "page.h"
#include "bookshelf.h"
#include <vector>

class BookshelfPage : public Page {
 public:
  void init();
  
  // Page interface
  void enter() override;
  void exit() override;
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;
  
 private:
  Bookshelf bookshelf;
  int bookshelfPage = 0;
  std::vector<int> fileBrowserItemYPositions;
  
  int getFileBrowserItemAt(int y) const;
  void nextBookshelfPage();
  void prevBookshelfPage();
  int getItemsPerPage() const;
};
