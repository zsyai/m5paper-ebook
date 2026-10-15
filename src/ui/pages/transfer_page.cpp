#include "ui/pages/transfer_page.h"

#include <M5Unified.h>
#include <SD.h>

#include <string>

#include "ui/pages/transfer_client_html.h"
#include "ui/widgets/bars.h"
#include "app/ui_manager.h"

extern UIManager ui;

namespace reader {
namespace ui {

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
  ip_addr_ = WiFi.softAPIP().toString();
  wifi_started_ = true;
}

void TransferPage::stopWiFi() {
  if (wifi_started_) {
    WiFi.softAPdisconnect(true);
    wifi_started_ = false;
  }
}

void TransferPage::startServer() {
  server_ = new WebServer(80);

  server_->on("/", HTTP_GET, [this]() {
    server_->send(200, "text/html", TRANSFER_CLIENT_HTML);
  });

  server_->on("/api/list", HTTP_GET, [this]() {
    String path = server_->arg("path");
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
        response +=
            "\"isDir\":" + String(file.isDirectory() ? "true" : "false") + ",";
        response += "\"size\":" + String(file.size());
        response += "}";

        file.close();
        file = root.openNextFile();
      }
      root.close();
    }
    response += "]";
    server_->send(200, "application/json", response);
  });

  server_->on("/api/delete", HTTP_POST, [this]() {
    String path = server_->arg("path");
    if (path.length() == 0) {
      server_->send(400, "text/plain", "Missing path");
      return;
    }

    if (!SD.exists(path)) {
      server_->send(404, "text/plain", "File not found");
      return;
    }

    File file = SD.open(path);
    if (file.isDirectory()) {
      file.close();
      server_->send(400, "text/plain", "Cannot delete directory");
      return;
    }
    file.close();

    if (SD.remove(path)) {
      server_->send(200, "text/plain", "Deleted");
    } else {
      server_->send(500, "text/plain", "Delete failed");
    }
  });

  server_->on(
      "/upload", HTTP_POST,
      [this]() { server_->send(200, "text/plain", "Upload complete"); },
      [this]() {
        HTTPUpload& upload = server_->upload();
        static File uploadFile;
        if (upload.status == UPLOAD_FILE_START) {
          String filename = upload.filename;
          if (filename.startsWith("/")) filename = filename.substring(1);

          String upload_dir = server_->arg("path");
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
            SD.mkdir(check_dir);
          }

          String path = check_dir + "/" + filename;
          uploadFile = SD.open(path, FILE_WRITE);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (uploadFile) {
            uploadFile.close();
          }
        }
      });

  server_->begin();

  xTaskCreate(
      [](void* p) {
        TransferPage* page = static_cast<TransferPage*>(p);
        while (true) {
          if (page->server_) {
            page->server_->handleClient();
          }
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      },
      "webserver_task", 4096, this, 1, &server_task_handle_);
}

void TransferPage::stopServer() {
  if (server_task_handle_) {
    vTaskDelete(server_task_handle_);
    server_task_handle_ = nullptr;
  }
  if (server_) {
    server_->stop();
    delete server_;
    server_ = nullptr;
  }
}

void TransferPage::draw(LGFX_Sprite& canvas) {
  UiPainter& p = ::ui.painter();
  canvas.fillScreen(TFT_WHITE);

  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int bar_h = ::ui.getHeaderHeight();

  std::string preload =
      "WiFi 传书结束SSID密码IP%0123456789.【】:：/"
      "无线指南第一步连接热点密码第二步打开浏览器访问第三步在网页中上传文件"
      "M5PaperAP12345678http";
  preload += ip_addr_.c_str();
  p.Prepare(preload);

  ::ui.drawHeader("WiFi 传书");

  const std::string title = "【 无线传书指南 】";
  const int step_gap = fs * 2.3f;
  const int line_gap = fs * 1.6f;

  const int title_y = bar_h + static_cast<int>(fs * 1.5f);
  p.Text(title, screen_w / 2 - p.Width(title) / 2, title_y);

  const int step1_y = title_y + step_gap;
  p.Text("第一步：连接 WiFi 热点", 40, step1_y);
  const int ssid_y = step1_y + line_gap;
  p.Text("      SSID：M5PaperAP", 40, ssid_y);
  const int pass_y = ssid_y + line_gap;
  p.Text("      密码：12345678", 40, pass_y);

  const int step2_y = pass_y + step_gap;
  p.Text("第二步：打开浏览器访问", 40, step2_y);
  const std::string ip_url = "      http://" + std::string(ip_addr_.c_str());
  const int url_y = step2_y + line_gap;
  p.Text(ip_url, 40, url_y);

  const int step3_y = url_y + step_gap;
  p.Text("第三步：在网页中上传文件", 40, step3_y);

  ::ui.drawBottomBar({"结束传书"});
}

State TransferPage::handleEvent(const SystemMessage_t& msg) {
  if (msg.type == MessageType::TOUCH) {
    if (BottomBarHit(::ui.getCanvas(), ::ui.getFooterHeight(), /*count=*/1,
                     msg.data.touch.x, msg.data.touch.y) == 0) {
      return State::BOOKSHELF;
    }
  }
  return State::TRANSFER;
}

}  // namespace ui
}  // namespace reader
