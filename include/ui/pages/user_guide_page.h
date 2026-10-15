#pragma once

// Built-in user guide page, laid out with book::MeasurePage and drawn with the
// UI font.

#include <string>
#include <vector>

#include "ui/page.h"
#include "ui/rect.h"

namespace reader {
namespace ui {

class UserGuidePage : public Page {
 public:
  void init() {}

  void enter() override;
  void exit() override {}
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;

 private:
  std::vector<std::string> pages_;
  std::string layout_font_path_;
  int current_page_idx_ = 0;
  Rect back_;

  void paginate(LGFX_Sprite& canvas);
};

}  // namespace ui
}  // namespace reader
