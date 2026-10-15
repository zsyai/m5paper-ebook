#pragma once

// Wi-Fi AP + HTTP file transfer page for uploading and managing /books, /fonts,
// and /images from a browser.

#include <WebServer.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ui/page.h"

namespace reader {
namespace ui {

class TransferPage : public Page {
 public:
  void init() {}

  void enter() override;
  void exit() override;
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;

 private:
  WebServer* server_ = nullptr;
  TaskHandle_t server_task_handle_ = nullptr;
  bool wifi_started_ = false;
  String ip_addr_;

  void startWiFi();
  void stopWiFi();
  void startServer();
  void stopServer();
};

}  // namespace ui
}  // namespace reader
