#pragma once

#include "page.h"
#include <vector>
#include <string>

class ImagePickerPage : public Page {
 public:
  // Page interface
  void enter() override;
  void exit() override;
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;
  
 private:
  std::vector<std::string> image_files;
  int current_image_index = -1;
  bool show_bars = true;
  
  void scanImages();
  void nextImage();
  void prevImage();
};
