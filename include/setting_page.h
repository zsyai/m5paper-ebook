#pragma once

#include "page.h"
#include <vector>

class SettingPage : public Page {
 public:
  void init();
  
  // Page interface
  void enter() override;
  void exit() override;
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;
  
  float getLineSpacing() const { return current_line_spacing; }
  
 private:
  float current_line_spacing = 2.0f;
  int initial_font_size = 0;
  std::vector<String> available_fonts;
  int current_font_index = 0;
  void scanFonts();
  void deleteIndexFiles();
  void cleanExpiredRecords();
  void loadSettings();
  void saveSettings();
};
