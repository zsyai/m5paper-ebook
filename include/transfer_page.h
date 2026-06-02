#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <WebServer.h>

#include "page.h"

class TransferPage : public Page {
 public:
  void init();

  // Page interface
  void enter() override;
  void exit() override;
  void draw(LGFX_Sprite& canvas) override;
  State handleEvent(const SystemMessage_t& msg) override;

 private:
  WebServer* server = nullptr;
  TaskHandle_t server_task_handle = nullptr;
  bool wifi_started = false;
  String ip_addr;

  void startWiFi();
  void stopWiFi();
  void startServer();
  void stopServer();
};
