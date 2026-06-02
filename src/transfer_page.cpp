#include "transfer_page.h"
#include "ui_manager.h"
#include "transfer_client_html.h"

#include <M5Unified.h>
#include <SD.h>

void TransferPage::init() {}

void TransferPage::enter() {
  if (!SD.exists("/images")) SD.mkdir("/images");
  if (!SD.exists("/fonts")) SD.mkdir("/fonts");
  if (!SD.exists("/books")) SD.mkdir("/books");

  startWiFi();
  startServer();
}

void TransferPage::exit() {
  stopServer();
  stopWiFi();
}

void TransferPage::startWiFi() {
  WiFi.softAP("M5PaperAP", "12345678");
  ip_addr = WiFi.softAPIP().toString();
  wifi_started = true;
}

void TransferPage::stopWiFi() {
  if (wifi_started) {
    WiFi.softAPdisconnect(true);
    wifi_started = false;
  }
}

void TransferPage::startServer() {
  server = new WebServer(80);

  server->on("/", HTTP_GET, [this]() {
    server->send(200, "text/html", TRANSFER_CLIENT_HTML);
  });

  server->on("/api/list", HTTP_GET, [this]() {
    String path = server->arg("path");
    if (path.length() == 0) path = "/";
    
    String response = "[";
    File root = SD.open(path);
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      bool first = true;
      while (file) {
        if (!first) response += ",";
        first = false;
        
        response += "{";
        response += "\"name\":\"" + String(file.name()) + "\",";
        response += "\"isDir\":" + String(file.isDirectory() ? "true" : "false") + ",";
        response += "\"size\":" + String(file.size());
        response += "}";
        
        file.close();
        file = root.openNextFile();
      }
      root.close();
    }
    response += "]";
    server->send(200, "application/json", response);
  });

  server->on("/api/delete", HTTP_POST, [this]() {
    String path = server->arg("path");
    if (path.length() == 0) {
      server->send(400, "text/plain", "Missing path");
      return;
    }
    
    if (!SD.exists(path)) {
      server->send(404, "text/plain", "File not found");
      return;
    }
    
    File file = SD.open(path);
    if (file.isDirectory()) {
      file.close();
      server->send(400, "text/plain", "Cannot delete directory");
      return;
    }
    file.close();
    
    if (SD.remove(path)) {
      server->send(200, "text/plain", "Deleted");
    } else {
      server->send(500, "text/plain", "Delete failed");
    }
  });

  server->on("/upload", HTTP_POST, [this]() {
    server->send(200, "text/plain", "Upload complete");
  }, [this]() {
    HTTPUpload& upload = server->upload();
    static File uploadFile;
    if (upload.status == UPLOAD_FILE_START) {
      String filename = upload.filename;
      if (filename.startsWith("/")) filename = filename.substring(1);
      
      String upload_dir = server->arg("path");
      if (upload_dir.length() == 0) {
        upload_dir = "/books";
      }
      
      if (!upload_dir.startsWith("/")) {
        upload_dir = "/" + upload_dir;
      }
      
      String check_dir = upload_dir;
      if (check_dir.endsWith("/")) {
        check_dir = check_dir.substring(0, check_dir.length() - 1);
      }
      
      if (!SD.exists(check_dir)) {
        Serial.print("Creating directory: "); Serial.println(check_dir);
        SD.mkdir(check_dir);
      }
      
      String path = check_dir + "/" + filename;
      Serial.print("Upload File Path: "); Serial.println(path);
      uploadFile = SD.open(path, FILE_WRITE);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (uploadFile) {
        uploadFile.write(upload.buf, upload.currentSize);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (uploadFile) {
        uploadFile.close();
      }
      Serial.println("Upload finished");
    }
  });

  server->begin();

  xTaskCreate(
      [](void* p) {
        TransferPage* page = (TransferPage*)p;
        while (1) {
          if (page->server) {
            page->server->handleClient();
          }
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      },
      "webserver_task",
      4096,
      this,
      1,
      &server_task_handle
  );
}

void TransferPage::stopServer() {
  if (server_task_handle) {
    vTaskDelete(server_task_handle);
    server_task_handle = nullptr;
  }
  if (server) {
    server->stop();
    delete server;
    server = nullptr;
  }
}

void TransferPage::draw(LGFX_Sprite& canvas) {
  canvas.fillScreen(TFT_WHITE);

  extern UIManager ui;

  int font_size = ui.getFontSize();
  int screen_w = canvas.width();
  int screen_h = canvas.height();

  // 1. Build Cache
  std::string all_text =
      "WiFi传书结束SSID密码IP%0123456789.【】:：/"
      "无线指南第一步连接热点密码第二步打开浏览器访问第三步在网页中上传文件"
      "M5PaperAP12345678http";
  all_text += ip_addr.c_str();
  ui.buildFontCache(all_text);

  // 2. Draw Header
  int bar_h = ui.getHeaderHeight();
  int text_y = (bar_h - font_size) / 2;
  
  ui.drawCachedString("WiFi 传书", 20, text_y);
  
  // Draw battery
  ui.drawBattery(canvas, canvas.width() - 20, text_y);
  
  int line_y = bar_h - 4;
  canvas.fillRect(0, line_y, screen_w, 4, TFT_BLACK);

  // 3. Draw Content (Middle - guided instructions split to fit screen width)
  std::string title = "【 无线传书指南 】";
  int txt_w = ui.getStringWidth(title);
  
  int step_gap = font_size * 2.3;
  int line_gap = font_size * 1.6;
  
  int title_y = bar_h + font_size * 1.5;
  ui.drawCachedString(title, screen_w / 2 - txt_w / 2, title_y);

  int step1_y = title_y + step_gap;
  ui.drawCachedString("第一步：连接 WiFi 热点", 40, step1_y);
  int ssid_y = step1_y + line_gap;
  ui.drawCachedString("      SSID：M5PaperAP", 40, ssid_y);
  int pass_y = ssid_y + line_gap;
  ui.drawCachedString("      密码：12345678", 40, pass_y);

  int step2_y = pass_y + step_gap;
  ui.drawCachedString("第二步：打开浏览器访问", 40, step2_y);
  std::string ip_url = "      http://" + std::string(ip_addr.c_str());
  int url_y = step2_y + line_gap;
  ui.drawCachedString(ip_url, 40, url_y);

  int step3_y = url_y + step_gap;
  ui.drawCachedString("第三步：在网页中上传文件", 40, step3_y);

  // 4. Draw Bottom Bar
  int bottom_bar_h = ui.getFooterHeight();
  int thick_line_y = screen_h - bottom_bar_h;
  canvas.fillRect(0, thick_line_y, screen_w, 4, TFT_BLACK);

  text_y = thick_line_y + (bottom_bar_h - font_size) / 2;

  std::string btn_label = "结束传书";
  txt_w = ui.getStringWidth(btn_label);
  ui.drawCachedString(btn_label, screen_w / 2 - txt_w / 2, text_y);
}

State TransferPage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type == MessageType::TOUCH) {
    int x = msg.data.touch.x;
    int y = msg.data.touch.y;

    extern UIManager ui;
    int bottom_bar_h = ui.getFooterHeight();
    int screen_h = M5.Display.height();
    if (y >= screen_h - bottom_bar_h) {
      return State::BOOKSHELF;  // Go back to bookshelf
    }
  }
  return State::TRANSFER;
}
