#include "ui/pages/image_picker_page.h"

#include <M5Unified.h>
#include <SD.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/app_context.h"
#include "book/jpeg_gray_decoder.h"
#include "hal/input.h"
#include "hal/storage.h"
#include "ui/widgets/bars.h"

namespace reader {
namespace ui {

namespace {

enum class ImageFormat { kUnknown, kPng, kJpg };

bool ProbeImage(const char* path, ImageFormat* format, int* width,
                int* height) {
  *format = ImageFormat::kUnknown;
  File file = SD.open(path, FILE_READ);
  if (!file) return false;

  uint8_t header[24];
  const int n = file.read(header, sizeof(header));
  if (n >= 24 && header[0] == 0x89 && header[1] == 'P' && header[2] == 'N' &&
      header[3] == 'G' && header[4] == 0x0D && header[5] == 0x0A &&
      header[6] == 0x1A && header[7] == 0x0A) {
    *width = (header[16] << 24) | (header[17] << 16) | (header[18] << 8) |
             header[19];
    *height = (header[20] << 24) | (header[21] << 16) | (header[22] << 8) |
              header[23];
    file.close();
    if (*width <= 0 || *height <= 0) return false;
    *format = ImageFormat::kPng;
    return true;
  }

  const uint32_t fsize = file.size();
  if (n < 2 || header[0] != 0xFF || header[1] != 0xD8 || !file.seek(2)) {
    file.close();
    return false;
  }

  while (file.position() < fsize) {
    uint8_t b = 0;
    if (file.read(&b, 1) != 1 || b != 0xFF) break;
    do {
      if (file.read(&b, 1) != 1) {
        file.close();
        return false;
      }
    } while (b == 0xFF);

    if (b == 0xD9 || b == 0xDA) break;
    if (b == 0x00 || b == 0x01 || (b >= 0xD0 && b <= 0xD8)) continue;

    uint8_t len_bytes[2];
    if (file.read(len_bytes, 2) != 2) break;
    const uint16_t len = (len_bytes[0] << 8) | len_bytes[1];
    if (len < 2) break;
    const uint32_t seg_pos = file.position();

    if (b == 0xC0 || b == 0xC1 || b == 0xC2) {
      uint8_t sof[5];
      if (len < 7 || file.read(sof, sizeof(sof)) != sizeof(sof)) break;
      *height = (sof[1] << 8) | sof[2];
      *width = (sof[3] << 8) | sof[4];
      file.close();
      if (*width <= 0 || *height <= 0) return false;
      *format = ImageFormat::kJpg;
      return true;
    }
    if (seg_pos + len - 2 > fsize || !file.seek(seg_pos + len - 2)) break;
  }
  file.close();
  return false;
}

bool ProbeImageMemory(const uint8_t* data, size_t size, ImageFormat* format,
                      int* width, int* height, uint8_t* jpeg_sof) {
  *format = ImageFormat::kUnknown;
  if (jpeg_sof != nullptr) *jpeg_sof = 0;
  if (!data || size < 4) return false;

  if (size >= 24 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G' && data[4] == 0x0D && data[5] == 0x0A &&
      data[6] == 0x1A && data[7] == 0x0A) {
    *width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    *height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    if (*width <= 0 || *height <= 0) return false;
    *format = ImageFormat::kPng;
    return true;
  }

  if (book::ProbeJpegHeader(data, size, width, height, jpeg_sof)) {
    *format = ImageFormat::kJpg;
    return true;
  }
  return false;
}

bool PushFallbackGrayscaleJpeg(LGFX_Sprite& canvas, const uint8_t* data,
                               size_t len, int x, int y, int draw_w,
                               int draw_h) {
  uint8_t* gray = nullptr;
  if (!book::DecodeJpegGrayscaleScaled(data, len, draw_w, draw_h, &gray)) {
    return false;
  }
  canvas.pushImage(x, y, draw_w, draw_h,
                   reinterpret_cast<const lgfx::grayscale_t*>(gray));
  std::free(gray);
  return true;
}

}  // namespace

bool DrawCenteredImageFromMemory(LGFX_Sprite& canvas, const uint8_t* data,
                                 size_t len, const Rect& bounds) {
  ImageFormat format = ImageFormat::kUnknown;
  int img_w = 0;
  int img_h = 0;
  uint8_t jpeg_sof = 0;
  if (bounds.w <= 0 || bounds.h <= 0 ||
      !ProbeImageMemory(data, len, &format, &img_w, &img_h, &jpeg_sof)) {
    return false;
  }
  const float scale =
      std::min({1.0f, static_cast<float>(bounds.w) / img_w,
                static_cast<float>(bounds.h) / img_h});
  const int draw_w = std::max(1, static_cast<int>(img_w * scale));
  const int draw_h = std::max(1, static_cast<int>(img_h * scale));
  const int x = bounds.x + (bounds.w - draw_w) / 2;
  const int y = bounds.y + (bounds.h - draw_h) / 2;
  const uint32_t ulen = static_cast<uint32_t>(len);
  if (format == ImageFormat::kPng) {
    return canvas.drawPng(data, ulen, x, y, 0, 0, 0, 0, scale, scale);
  }
  if (jpeg_sof == 0xC0 &&
      canvas.drawJpg(data, ulen, x, y, 0, 0, 0, 0, scale, scale)) {
    return true;
  }
  return PushFallbackGrayscaleJpeg(canvas, data, len, x, y, draw_w, draw_h);
}

bool DrawCenteredImage(LGFX_Sprite& canvas, const std::string& path) {
  ImageFormat format = ImageFormat::kUnknown;
  int img_w = 0;
  int img_h = 0;
  if (!ProbeImage(path.c_str(), &format, &img_w, &img_h)) return false;

  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const float scale = std::min({1.0f, static_cast<float>(screen_w) / img_w,
                                static_cast<float>(screen_h) / img_h});
  const int draw_w = std::max(1, static_cast<int>(img_w * scale));
  const int draw_h = std::max(1, static_cast<int>(img_h * scale));
  const int x = (screen_w - draw_w) / 2;
  const int y = (screen_h - draw_h) / 2;

  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return false;
  const uint32_t fsize = f.size();
  bool drawn = (format == ImageFormat::kPng)
                   ? canvas.drawPng(&f, x, y, 0, 0, 0, 0, scale, scale)
                   : canvas.drawJpg(&f, x, y, 0, 0, 0, 0, scale, scale);
  if (!drawn && format == ImageFormat::kJpg && fsize > 0 &&
      fsize <= 1536 * 1024 && f.seek(0)) {
    uint8_t* buf = static_cast<uint8_t*>(ps_malloc(fsize));
    if (buf != nullptr) {
      if (static_cast<uint32_t>(f.read(buf, fsize)) == fsize) {
        drawn = PushFallbackGrayscaleJpeg(canvas, buf, fsize, x, y, draw_w,
                                          draw_h);
      }
      std::free(buf);
    }
  }
  f.close();
  return drawn;
}

void ImagePickerPage::scanImages() {
  image_files_.clear();
  current_image_index_ = -1;

  if (!SD.exists("/images")) {
    SD.mkdir("/images");
    return;
  }

  File dir = SD.open("/images");
  if (!dir) {
    hal::ReportSdError();
    return;
  }
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

void ImagePickerPage::enter(app::AppContext& /*ctx*/) {
  scanImages();
  show_bars_ = true;
}

void ImagePickerPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  UiPainter& p = ctx.painter();
  canvas.fillScreen(TFT_WHITE);

  const int screen_h = canvas.height();

  if (current_image_index_ >= 0 &&
      current_image_index_ < static_cast<int>(image_files_.size())) {
    const std::string& path = image_files_[current_image_index_];
    if (!DrawCenteredImage(canvas, path)) {
      p.Prepare("图片解析失败");
      p.Text("图片解析失败", 20, screen_h / 2);
    }
  } else {
    p.Prepare("无图片文件");
    p.Text("无图片文件", 20, screen_h / 2);
  }

  back_ = Rect{};
  if (show_bars_) {
    back_ = DrawBackHeader(p, ctx.header_height(), ctx.power.BatteryLevel());
    if (!image_files_.empty()) {
      const bool is_current =
          image_files_[current_image_index_] == ctx.settings.lock_screen_path;
      ctx.DrawBottomBar({is_current ? "当前选择" : "设为锁屏"});
    }
  }
}

app::Nav ImagePickerPage::handleEvent(const app::Event& msg,
                                      app::AppContext& ctx) {
  if (msg.type == app::EventType::kWheel) {
    if (msg.wheel.delta > 0) {
      nextImage();
      ctx.DrawCurrentPage();
    } else if (msg.wheel.delta < 0) {
      prevImage();
      ctx.DrawCurrentPage();
    }
    return app::Nav::Stay();
  }

  if (msg.type == app::EventType::kSwipe) {
    const hal::Swipe swipe = hal::ClassifySwipe(msg.swipe.dx, msg.swipe.dy);
    if (swipe == hal::Swipe::kLeft || swipe == hal::Swipe::kRight) {
      swipe == hal::Swipe::kLeft ? nextImage() : prevImage();
      ctx.DrawCurrentPage();
    }
    return app::Nav::Stay();
  }
  if (msg.type != app::EventType::kTap) return app::Nav::Stay();
  const int x = msg.tap.x;
  const int y = msg.tap.y;
  const LGFX_Sprite& canvas = ctx.canvas();
  const int screen_w = canvas.width();
  const int screen_h = canvas.height();
  const int header_h = ctx.header_height();
  const int footer_h = ctx.footer_height();

  if (show_bars_) {
    if (back_.Contains(x, y)) return app::Nav::Back();
    if (BottomBarHit(canvas, footer_h, /*count=*/1, x, y) == 0) {
      if (current_image_index_ >= 0 &&
          current_image_index_ < static_cast<int>(image_files_.size())) {
        const std::string& current_path = image_files_[current_image_index_];
        if (current_path != ctx.settings.lock_screen_path) {
          ctx.settings.lock_screen_path = current_path;
          ctx.settings.lock_screen_enabled = true;
          ctx.SaveSettings();
          return app::Nav::Back();
        }
      }
      return app::Nav::Stay();
    }
  }

  if (y >= header_h && y <= screen_h - footer_h) {
    if (x < screen_w / 3) {
      prevImage();
      ctx.DrawCurrentPage();
    } else if (x > 2 * screen_w / 3) {
      nextImage();
      ctx.DrawCurrentPage();
    } else {
      show_bars_ = !show_bars_;
      ctx.DrawCurrentPage();
    }
  }
  return app::Nav::Stay();
}

void ImagePickerPage::nextImage() {
  if (image_files_.empty()) return;
  current_image_index_ =
      (current_image_index_ + 1) % static_cast<int>(image_files_.size());
}

void ImagePickerPage::prevImage() {
  if (image_files_.empty()) return;
  const int n = static_cast<int>(image_files_.size());
  current_image_index_ = (current_image_index_ - 1 + n) % n;
}

}  // namespace ui
}  // namespace reader
