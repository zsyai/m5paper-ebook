#include "ui/pages/transfer_page.h"

#include <M5Unified.h>
#include <SD.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/utime.h>

#include <cstdlib>
#include <ctime>
#include <string>

#include "app/app_context.h"
#include "core/sd_path.h"
#include "i18n/i18n.h"
#include "ui/widgets/bars.h"

extern const char kTransferClientHtml[] asm(
    "_binary_assets_transfer_client_html_start");
// French page, built by tools/translate_html_fr.py.
extern const char kTransferClientHtmlFr[] asm(
    "_binary_assets_transfer_client_fr_html_start");

namespace reader {
namespace ui {

namespace {

constexpr char kUploadTempPath[] = "/configs/.upload.tmp";
constexpr uint32_t kUploadStallTimeoutMs = 15000;

}  // namespace

void TransferPage::enter(app::AppContext& ctx) {
  ctx.book.CloseForTransfer();
  ctx.fonts.CancelBackgroundBuilds();
  if (!SD.exists("/configs")) SD.mkdir("/configs");
  if (!SD.exists("/images")) SD.mkdir("/images");
  if (!SD.exists("/fonts")) SD.mkdir("/fonts");
  if (!SD.exists("/books")) SD.mkdir("/books");
  if (SD.exists(kUploadTempPath)) SD.remove(kUploadTempPath);

  startWiFi();
  startServer();
}

void TransferPage::exit(app::AppContext& /*ctx*/) {
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
    const bool fr = i18n::CurrentLanguage() == i18n::Language::kFr;
    server_->send(200, "text/html",
                  fr ? kTransferClientHtmlFr : kTransferClientHtml);
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
        String name = file.name();
        if (name.startsWith(".")) {
          file.close();
          file = root.openNextFile();
          continue;
        }
        if (!first) response += ",";
        first = false;

        response += "{";
        response += "\"name\":\"" + name + "\",";
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
      [this]() {
        if (upload_failed_) {
          server_->send(500, "text/plain", "Write failed");
        } else {
          server_->send(200, "text/plain", "Upload complete");
        }
      },
      [this]() {
        HTTPUpload& upload = server_->upload();
        if (upload.status == UPLOAD_FILE_START) {
          if (upload_file_) upload_file_.close();

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

          upload_ts_ = 0;
          if (server_->hasArg("ts")) {
            const unsigned long parsed =
                std::strtoul(server_->arg("ts").c_str(), nullptr, 10);
            if (parsed >= 1700000000UL) {
              upload_ts_ = static_cast<time_t>(parsed);
              const time_t now_ts = std::time(nullptr);
              if (std::llabs(static_cast<long long>(now_ts) -
                             static_cast<long long>(upload_ts_)) > 60) {
                struct timeval tv;
                tv.tv_sec = upload_ts_;
                tv.tv_usec = 0;
                ::settimeofday(&tv, nullptr);
              }
            }
          }

          upload_path_ = check_dir + "/" + filename;
          upload_file_ = SD.open(kUploadTempPath, FILE_WRITE);
          upload_failed_ = !upload_file_;
          last_upload_ms_.store(millis());
          upload_fd_.store(server_->client().fd());
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          last_upload_ms_.store(millis());
          if (!upload_failed_ &&
              (!upload_file_ ||
               upload_file_.write(upload.buf, upload.currentSize) !=
                   upload.currentSize)) {
            upload_failed_ = true;
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          upload_fd_.store(-1);
          if (upload_file_) upload_file_.close();
          if (!upload_failed_) {
            if (SD.exists(upload_path_)) SD.remove(upload_path_);
            if (!SD.rename(kUploadTempPath, upload_path_)) {
              upload_failed_ = true;
            } else if (upload_ts_ > 0) {
              struct utimbuf times;
              times.actime = upload_ts_;
              times.modtime = upload_ts_;
              ::utime(core::SdPath(upload_path_.c_str()).c_str(), &times);
            }
          }
          if (upload_failed_ && SD.exists(kUploadTempPath)) {
            SD.remove(kUploadTempPath);
          }
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
          upload_fd_.store(-1);
          upload_failed_ = true;
          abortUploadFile();
        }
      });

  server_->begin();

  stop_server_.store(false);
  upload_fd_.store(-1);
  server_done_sem_ = xSemaphoreCreateBinary();
  xTaskCreate(
      [](void* p) {
        TransferPage* page = static_cast<TransferPage*>(p);
        while (!page->stop_server_.load()) {
          if (page->server_) {
            page->server_->handleClient();
          }
          vTaskDelay(pdMS_TO_TICKS(5));
        }
        xSemaphoreGive(page->server_done_sem_);
        vTaskDelete(nullptr);
      },
      "webserver_task", 4096, this, 1, &server_task_handle_);
}

void TransferPage::abortUploadFile() {
  if (upload_file_) upload_file_.close();
  if (SD.exists(kUploadTempPath)) SD.remove(kUploadTempPath);
}

void TransferPage::stopServer() {
  stop_server_.store(true);
  const int fd = upload_fd_.exchange(-1);
  if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
  stopWiFi();

  if (server_task_handle_) {
    if (!server_done_sem_ ||
        xSemaphoreTake(server_done_sem_, pdMS_TO_TICKS(3000)) != pdTRUE) {
      vTaskDelete(server_task_handle_);
    }
    server_task_handle_ = nullptr;
  }
  if (server_done_sem_) {
    vSemaphoreDelete(server_done_sem_);
    server_done_sem_ = nullptr;
  }
  abortUploadFile();
  if (server_) {
    server_->stop();
    delete server_;
    server_ = nullptr;
  }
}

void TransferPage::draw(app::AppContext& ctx, LGFX_Sprite& canvas) {
  UiPainter& p = ctx.painter();
  canvas.fillScreen(TFT_WHITE);

  const int fs = p.font_size();
  const int screen_w = canvas.width();
  const int bar_h = ctx.header_height();

  std::string preload = i18n::GlyphPreload(
      "WiFi 传书结束SSID密码IP%0123456789.【】:：/"
      "无线指南第一步连接热点密码第二步打开浏览器访问第三步在网页中上传文件");
  preload += "M5PaperAP12345678http";
  preload += ip_addr_.c_str();
  p.Prepare(preload);

  ctx.DrawHeader(TR("WiFi 传书"));

  const std::string title = TR("【 无线传书指南 】");
  const int step_gap = static_cast<int>(fs * 2.3f);
  const int line_gap = static_cast<int>(fs * 1.6f);

  const int title_y = bar_h + static_cast<int>(fs * 1.5f);
  p.Text(title, screen_w / 2 - p.Width(title) / 2, title_y);

  const int step1_y = title_y + step_gap;
  p.Text(TR("第一步：连接 WiFi 热点"), 40, step1_y);
  const int ssid_y = step1_y + line_gap;
  p.Text(TR("      SSID：M5PaperAP"), 40, ssid_y);
  const int pass_y = ssid_y + line_gap;
  p.Text(TR("      密码：12345678"), 40, pass_y);

  const int step2_y = pass_y + step_gap;
  p.Text(TR("第二步：打开浏览器访问"), 40, step2_y);
  const std::string ip_url = "      http://" + std::string(ip_addr_.c_str());
  const int url_y = step2_y + line_gap;
  p.Text(ip_url, 40, url_y);

  const int step3_y = url_y + step_gap;
  p.Text(TR("第三步：在网页中上传文件"), 40, step3_y);

  ctx.DrawBottomBar({TR("结束传书")});
}

app::Nav TransferPage::handleEvent(const app::Event& msg,
                                   app::AppContext& ctx) {
  if (msg.type == app::EventType::kTick) {
    int fd = upload_fd_.load();
    if (fd >= 0 && millis() - last_upload_ms_.load() > kUploadStallTimeoutMs) {
      if (upload_fd_.compare_exchange_strong(fd, -1)) {
        ::shutdown(fd, SHUT_RDWR);
      }
    }
    return app::Nav::Stay();
  }
  if (msg.type == app::EventType::kTap) {
    if (BottomBarHit(ctx.canvas(), ctx.footer_height(), /*count=*/1, msg.tap.x,
                     msg.tap.y) == 0) {
      return app::Nav::Root(State::BOOKSHELF);
    }
  }
  return app::Nav::Stay();
}

}  // namespace ui
}  // namespace reader
