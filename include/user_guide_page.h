#pragma once

#include "page.h"
#include <vector>
#include <string>

class UserGuidePage : public Page {
 public:
  void init();
  
  // Page interface
  void enter() override;
  void exit() override;
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;
  
 private:
  std::vector<std::string> pages;
  int current_page_idx = 0;
  
  void paginate(LGFX_Sprite& canvas);
  static bool cannotStartLine(uint32_t unicode);
};
