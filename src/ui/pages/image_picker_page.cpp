#include "ui/pages/image_picker_page.h"

#include <M5Unified.h>
#include <SD.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "ui/widgets/bars.h"
#include "app/ui_manager.h"

extern UIManager ui;

namespace reader {
namespace ui {

namespace {

bool IsPngPath(const std::string& path) {
  return path.find(".png") != std::string::npos ||
         path.find(".PNG") != std::string::npos;
}

#ifdef HOST_SIMULATOR
bool GetJpgSize(const char* path, int* width, int* height) {
  *width = 400;
  *height = 300;
  return true;
}

bool GetPngSize(const char* path, int* width, int* height) {
  *width = 400;
  *height = 300;
  return true;
}
#else
bool GetJpgSize(const char* path, int* width, int* height) {
  File file = SD.open(path, FILE_READ);
  if (!file) return false;

  uint8_t marker[2];
  if (file.read(marker, 2) != 2 || marker[0] != 0xFF || marker[1] != 0xD8) {
    file.close();
    return false;
  }

  while (file.available()) {
    if (file.read(marker, 2) != 2 || marker[0] != 0xFF) break;
    const uint8_t m = marker[1];
    if (m == 0xD9) break;  // EOI

    uint8_t len_bytes[2];
    if (file.read(len_bytes, 2) != 2) break;
    const uint16_t len = (len_bytes[0] << 8) | len_bytes[1];

    if (m == 0xC0 || m == 0xC2) {  // SOF0 or SOF2
      uint8_t precision;
      uint8_t h_bytes[2];
      uint8_t w_bytes[2];
      if (file.read(&precision, 1) != 1 || file.read(h_bytes, 2) != 2 ||
          file.read(w_bytes, 2) != 2) {
        break;
      }
      *height = (h_bytes[0] << 8) | h_bytes[1];
      *width = (w_bytes[0] << 8) | w_bytes[1];
      file.close();
      return true;
    }
    file.seek(file.position() + len - 2);
  }
  file.close();
  return false;
}

bool GetPngSize(const char* path, int* width, int* height) {
  File file = SD.open(path, FILE_READ);
  if (!file) return false;

  uint8_t header[24];
  if (file.read(header, 24) != 24 || header[0] != 0x89 || header[1] != 'P' ||
      header[2] != 'N' || header[3] != 'G') {
    file.close();
    return false;
  }

  *width =
      (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];
  *height =
      (header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23];
  file.close();
  return true;
}
#endif

}  // namespace

void ImagePickerPage::scanImages() {
  image_files_.clear();
  current_image_index_ = -1;

  if (!SD.exists("/images")) {
    SD.mkdir("/images");
    return;
  }

  File dir = SD.open("/images");
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      const std::string name = entry.name();
      const size_t dot = name.find_last_of('.');
      if (dot != std::string::npos) {
        std::string ext = name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext == "jpg" || ext == "jpeg" || ext == "png") {
          image_files_.push_back("/images/" + name);
        }
      }
    }
    entry.close();
  }
  dir.close();

  if (!image_files_.empty()) {
    current_image_index_ = 0;
  }
}

void ImagePickerPage::enter() {
  scanImages();
  show_bars_ = true;
}

void ImagePickerPage::draw(LGFX_Sprite& canvas) {
  UiPainter& p = ::ui.painter();
  canvas.fillScreen(TFT_WHITE);

  const int screen_w = canvas.width();
  const int screen_h = canvas.height();

  if (current_image_index_ >= 0 &&
      current_image_index_ < static_cast<int>(image_files_.size())) {
    const std::string& path = image_files_[current_image_index_];
    int img_w = 0;
    int img_h = 0;
    const bool is_png = IsPngPath(path);
    const bool ok = is_png ? GetPngSize(path.c_str(), &img_w, &img_h)
                           : GetJpgSize(path.c_str(), &img_w, &img_h);

    bool drawn = false;
    if (ok && img_w > 0 && img_h > 0) {
      const float scale = std::min(static_cast<float>(screen_w) / img_w,
                                   static_cast<float>(screen_h) / img_h);
      const int x = (screen_w - static_cast<int>(img_w * scale)) / 2;
      const int y = (screen_h - static_cast<int>(img_h * scale)) / 2;

      File f = SD.open(path.c_str(), FILE_READ);
      if (f) {
        drawn = is_png ? canvas.drawPng(&f, x, y) : canvas.drawJpg(&f, x, y);
        f.close();
      }
    }
    if (!drawn) {
      p.Prepare("图片解析失败");
      p.Text("图片解析失败", 20, screen_h / 2);
    }
  } else {
    p.Prepare("无图片文件");
    p.Text("无图片文件", 20, screen_h / 2);
  }

  back_ = Rect{};
  if (show_bars_) {
    back_ = DrawBackHeader(p, ::ui.getHeaderHeight(),
                           M5.Power.getBatteryLevel());
    if (!image_files_.empty()) {
      const bool is_current =
          image_files_[current_image_index_] == ::ui.getLockScreenPath();
      ::ui.drawBottomBar({is_current ? "当前选择" : "设为锁屏"});
    }
  }
}

State ImagePickerPage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type == MessageType::WHEEL) {
    if (msg.data.wheel.delta > 0) {
      nextImage();
      ::ui.drawCurrentPage();
    } else if (msg.data.wheel.delta < 0) {
      prevImage();
      ::ui.drawCurrentPage();
    }
    return State::IMAGE_PICKER;
  }

  if (msg.type != MessageType::TOUCH) return State::IMAGE_PICKER;
  const int x = msg.data.touch.x;
  const int y = msg.data.touch.y;
  const LGFX_Sprite& canvas = ::ui.getCanvas();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const int header_h = ::ui.getHeaderHeight();
  const int footer_h = ::ui.getFooterHeight();

  if (show_bars_) {
    if (back_.Contains(x, y)) return State::SETTING;
    if (BottomBarHit(canvas, footer_h, /*count=*/1, x, y) == 0) {
      if (current_image_index_ >= 0 &&
          current_image_index_ < static_cast<int>(image_files_.size())) {
        const std::string& current_path = image_files_[current_image_index_];
        if (current_path != ::ui.getLockScreenPath()) {
          ::ui.setLockScreenPath(current_path);
          ::ui.setLockScreenEnabled(true);
          ::ui.saveGlobalConfig();
          return State::SETTING;
        }
      }
      return State::IMAGE_PICKER;
    }
  }

  if (y >= header_h && y <= screen_h - footer_h) {
    if (x < screen_w / 3) {
      prevImage();
      ::ui.drawCurrentPage();
    } else if (x > 2 * screen_w / 3) {
      nextImage();
      ::ui.drawCurrentPage();
    } else {
      show_bars_ = !show_bars_;
      ::ui.drawCurrentPage();
    }
  }
  return State::IMAGE_PICKER;
}

void ImagePickerPage::nextImage() {
  if (image_files_.empty()) return;
  current_image_index_ = (current_image_index_ + 1) % image_files_.size();
}

void ImagePickerPage::prevImage() {
  if (image_files_.empty()) return;
  current_image_index_ =
      (current_image_index_ - 1 + image_files_.size()) % image_files_.size();
}

}  // namespace ui
}  // namespace reader
