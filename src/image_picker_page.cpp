#include "image_picker_page.h"
#include "ui_manager.h"
#include <M5Unified.h>
#include <SD.h>
#include <vector>
#include <string>
#include <algorithm>

extern UIManager ui;



// Helper to get JPG dimensions
#ifdef HOST_SIMULATOR
static bool getJpgSize(const char* path, int& width, int& height) {
    width = 400;
    height = 300;
    return true;
}
#else
static bool getJpgSize(const char* path, int& width, int& height) {
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    
    uint8_t marker[2];
    if (file.read(marker, 2) != 2 || marker[0] != 0xFF || marker[1] != 0xD8) {
        file.close();
        return false;
    }
    
    while (file.available()) {
        if (file.read(marker, 2) != 2 || marker[0] != 0xFF) {
            break;
        }
        
        uint8_t m = marker[1];
        if (m == 0xD9) break; // EOI
        
        uint8_t len_bytes[2];
        if (file.read(len_bytes, 2) != 2) break;
        uint16_t len = (len_bytes[0] << 8) | len_bytes[1];
        
        if (m == 0xC0 || m == 0xC2) { // SOF0 or SOF2
            uint8_t precision;
            uint8_t h_bytes[2];
            uint8_t w_bytes[2];
            if (file.read(&precision, 1) != 1 ||
                file.read(h_bytes, 2) != 2 ||
                file.read(w_bytes, 2) != 2) {
                break;
            }
            height = (h_bytes[0] << 8) | h_bytes[1];
            width = (w_bytes[0] << 8) | w_bytes[1];
            file.close();
            return true;
        } else {
            file.seek(file.position() + len - 2);
        }
    }
    file.close();
    return false;
}
#endif

// Helper to get PNG dimensions
#ifdef HOST_SIMULATOR
static bool getPngSize(const char* path, int& width, int& height) {
    width = 400;
    height = 300;
    return true;
}
#else
static bool getPngSize(const char* path, int& width, int& height) {
    File file = SD.open(path, FILE_READ);
    if (!file) {
        Serial.println("Failed to open PNG file");
        return false;
    }
    
    uint8_t header[24];
    if (file.read(header, 24) != 24) {
        Serial.println("Failed to read PNG header");
        file.close();
        return false;
    }
    
    if (header[0] != 0x89 || header[1] != 'P' || header[2] != 'N' || header[3] != 'G') {
        Serial.printf("Invalid PNG signature: %02X %02X %02X %02X\n", header[0], header[1], header[2], header[3]);
        file.close();
        return false;
    }
    
    width = (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];
    height = (header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23];
    
    file.close();
    return true;
}
#endif

void ImagePickerPage::scanImages() {
    image_files.clear();
    current_image_index = -1;

    if (!SD.exists("/images")) {
        SD.mkdir("/images");
        return;
    }

    File dir = SD.open("/images");
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        
        if (!entry.isDirectory()) {
            std::string name = entry.name();
            std::string full_path = "/images/" + name;
            
            // Convert to lowercase for extension check
            std::string ext = "";
            size_t dot = name.find_last_of('.');
            if (dot != std::string::npos) {
                ext = name.substr(dot + 1);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            }
            
            if (ext == "jpg" || ext == "jpeg" || ext == "png") {
                image_files.push_back(full_path);
            }
        }
        entry.close();
    }
    dir.close();

    if (!image_files.empty()) {
        current_image_index = 0;
    }
}

void ImagePickerPage::enter() {
    scanImages();
    show_bars = true;
}

void ImagePickerPage::exit() {
}

void ImagePickerPage::draw(LGFX_Sprite& canvas) {
    canvas.fillScreen(TFT_WHITE);
    
    int screen_w = canvas.width();
    int screen_h = canvas.height();
    
    if (current_image_index >= 0 && current_image_index < image_files.size()) {
        std::string path = image_files[current_image_index];
        int img_w = 0, img_h = 0;
        bool ok = false;
        
        if (path.find(".png") != std::string::npos || path.find(".PNG") != std::string::npos) {
            ok = getPngSize(path.c_str(), img_w, img_h);
        } else {
            ok = getJpgSize(path.c_str(), img_w, img_h);
        }
        
        if (ok && img_w > 0 && img_h > 0) {
            // Scheme B: Scale proportionally to fit, leave white space
            float scale_x = (float)screen_w / img_w;
            float scale_y = (float)screen_h / img_h;
            float scale = std::min(scale_x, scale_y);
            
            int new_w = img_w * scale;
            int new_h = img_h * scale;
            int x = (screen_w - new_w) / 2;
            int y = (screen_h - new_h) / 2;
            
            File f = SD.open(path.c_str(), FILE_READ);
            if (f) {
                if (path.find(".png") != std::string::npos || path.find(".PNG") != std::string::npos) {
                    canvas.drawPng(&f, x, y);
                } else {
                    canvas.drawJpg(&f, x, y);
                }
                f.close();
            } else {
                ui.buildFontCache("图片解析失败");
                ui.drawCachedString("图片解析失败", 20, screen_h / 2);
            }
        } else {
            ui.buildFontCache("图片解析失败");
            ui.drawCachedString("图片解析失败", 20, screen_h / 2);
        }
    } else {
        ui.buildFontCache("无图片文件");
        ui.drawCachedString("无图片文件", 20, screen_h / 2);
    }
    
    if (show_bars) {
        // Draw Top Bar
        int bar_h = ui.getHeaderHeight();
        canvas.fillRect(0, 0, screen_w, bar_h, TFT_WHITE);
        canvas.fillRect(0, bar_h - 4, screen_w, 4, TFT_BLACK);
        
        int col_w = screen_w / 2;
        
        // Top Left: "返回"
        std::string back_text = "返回";
        ui.buildFontCache(back_text + "图片预览");
        int back_w = ui.getStringWidth(back_text);
        ui.drawCachedString(back_text, col_w / 2 - back_w / 2, (bar_h - ui.getFontSize()) / 2);
        
        // Draw vertical separator for "返回" button
        canvas.drawLine(col_w, 0, col_w, bar_h, TFT_BLACK);
        
        // Top Right: Battery
        ui.drawBattery(canvas, screen_w - 20, (bar_h - ui.getFontSize()) / 2);
        
        // Draw Bottom Bar only if there are images
        if (!image_files.empty()) {
            int footer_h = ui.getFooterHeight();
            int footer_y = screen_h - footer_h;
            canvas.fillRect(0, footer_y, screen_w, footer_h, TFT_WHITE);
            canvas.fillRect(0, footer_y, screen_w, 4, TFT_BLACK);
            
            std::string current_path = image_files[current_image_index];
            std::string saved_path = ui.getLockScreenPath();
            bool is_current = (current_path == saved_path);
            
            std::string btn_text = is_current ? "当前选择" : "设为锁屏";
            ui.buildFontCache(btn_text);
            int txt_w = ui.getStringWidth(btn_text);
            ui.drawCachedString(btn_text, (screen_w - txt_w) / 2, footer_y + (footer_h - ui.getFontSize()) / 2);
        }
    }
}

State ImagePickerPage::handleEvent(const SystemMessage_t& msg) {
    if (msg.type == MessageType::TOUCH) {
        int x = msg.data.touch.x;
        int y = msg.data.touch.y;
        
        int screen_w = M5.Display.width();
        int screen_h = M5.Display.height();
        
        int header_h = ui.getHeaderHeight();
        int footer_h = ui.getFooterHeight();
        
        if (show_bars) {
            // Handle top bar
            if (y < header_h) {
                if (x < screen_w / 2) {
                    return State::SETTING;
                }
            }
            // Handle bottom bar
            else if (y > screen_h - footer_h) {
                if (current_image_index >= 0 && current_image_index < image_files.size()) {
                    std::string current_path = image_files[current_image_index];
                    std::string saved_path = ui.getLockScreenPath();
                    if (current_path != saved_path) {
                        ui.setLockScreenPath(current_path);
                        ui.setLockScreenEnabled(true);
                        ui.saveGlobalConfig();
                        return State::SETTING;
                    }
                }
                // If it is current, do nothing (disabled)
            }
        }
        
        // Handle center tap to toggle bars
        if (y >= header_h && y <= screen_h - footer_h) {
            if (x >= screen_w / 3 && x <= 2 * screen_w / 3) {
                show_bars = !show_bars;
                ui.drawCurrentPage();
                return State::IMAGE_PICKER;
            }
            // Handle left/right tap for page turn
            else if (x < screen_w / 3) {
                prevImage();
                ui.drawCurrentPage();
            } else if (x > 2 * screen_w / 3) {
                nextImage();
                ui.drawCurrentPage();
            }
        }
    } else if (msg.type == MessageType::WHEEL) {
        if (msg.data.wheel.delta > 0) {
            nextImage();
            ui.drawCurrentPage();
        } else if (msg.data.wheel.delta < 0) {
            prevImage();
            ui.drawCurrentPage();
        }
    }
    return State::IMAGE_PICKER;
}

void ImagePickerPage::nextImage() {
    if (image_files.empty()) return;
    current_image_index = (current_image_index + 1) % image_files.size();
}

void ImagePickerPage::prevImage() {
    if (image_files.empty()) return;
    current_image_index = (current_image_index - 1 + image_files.size()) % image_files.size();
}
