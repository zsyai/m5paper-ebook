#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_TRANSFER_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_TRANSFER_PAGE_H_

// Wi-Fi AP + HTTP file transfer page for uploading and managing /books, /fonts,
// and /images from a browser.

#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <ctime>

#include "ui/page.h"

namespace reader {
namespace ui {

class TransferPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void exit(app::AppContext& ctx) override;
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override;
  app::Nav handleEvent(const app::Event& msg, app::AppContext& ctx) override;

 private:
  WebServer* server_ = nullptr;
  TaskHandle_t server_task_handle_ = nullptr;
  SemaphoreHandle_t server_done_sem_ = nullptr;
  std::atomic<bool> stop_server_{false};
  std::atomic<int> upload_fd_{-1};
  std::atomic<uint32_t> last_upload_ms_{0};
  File upload_file_;
  String upload_path_;
  time_t upload_ts_ = 0;
  bool upload_failed_ = false;
  bool wifi_started_ = false;
  String ip_addr_;

  void startWiFi();
  void stopWiFi();
  void startServer();
  void stopServer();
  void abortUploadFile();
};

}  // namespace ui
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_TRANSFER_PAGE_H_
