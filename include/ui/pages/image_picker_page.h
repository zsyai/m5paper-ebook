#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_IMAGE_PICKER_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_IMAGE_PICKER_PAGE_H_

// Lock-screen wallpaper picker page (/images/*.jpg, *.jpeg, *.png).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ui/page.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

bool DrawCenteredImage(LGFX_Sprite& canvas, const std::string& path);
bool DrawCenteredImageFromMemory(LGFX_Sprite& canvas, const uint8_t* data,
                                 size_t len, const Rect& bounds);

class ImagePickerPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

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

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_IMAGE_PICKER_PAGE_H_
