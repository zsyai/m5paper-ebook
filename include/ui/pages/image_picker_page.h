#pragma once

// Lock-screen wallpaper picker page (/images/*.jpg, *.jpeg, *.png).

#include <string>
#include <vector>

#include "ui/page.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

class ImagePickerPage : public Page {
 public:
  void enter() override;
  void exit() override {}
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;

 private:
  std::vector<std::string> image_files_;
  int current_image_index_ = -1;
  bool show_bars_ = true;
  Rect back_;

  void scanImages();
  void nextImage();
  void prevImage();
};

}  // namespace ui
}  // namespace reader
