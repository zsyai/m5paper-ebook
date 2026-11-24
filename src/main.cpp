#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "app/app.h"
#include "app/state_machine.h"
#include "core/diag_log.h"
#include "core/perf_trace.h"
#include "core/sd_bench.h"
#include "hal/power.h"
#include "hal/storage.h"

#define SD_SPI_SCK_PIN 14
#define SD_SPI_MISO_PIN 13
#define SD_SPI_MOSI_PIN 12
#define SD_SPI_CS_PIN 4

reader::app::App g_app;

void setup() {
  PERF_SCOPE("setup");
  reader::hal::LogBootTiming("setup_start");
  log_i("[boot] reset_reason=%d wakeup_cause=%d",
        static_cast<int>(esp_reset_reason()),
        static_cast<int>(esp_sleep_get_wakeup_cause()));
  auto cfg = M5.config();
  // Boot-time savings: the first frame is a full GC16 refresh anyway, so skip
  // the panel clear and skip probing peripherals M5Paper does not have.
  cfg.clear_display = false;
  cfg.internal_imu = false;
  cfg.internal_mic = false;
  cfg.internal_spk = false;
#if READER_PERF
  cfg.serial_baudrate = 115200;
#endif
  {
    PERF_SCOPE("M5.begin");
    M5.begin(cfg);
  }
#if READER_PERF
  while (Serial.available() > 0) {
    Serial.read();
  }
#endif
  reader::hal::LogBootTiming("after_m5_begin");
  PERF_MARK("setup.after_m5_begin");

  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  g_app.storage().MountInitial();
  reader::core::InitDiagLog();

  init_state_machine();
  g_app.Init(&g_state_machine_queue);
  PERF_MARK("setup.ui_ready");

  if (!g_app.RestoreLastBookAtBoot()) {
    PERF_SCOPE("boot.bookshelf");
    g_app.SetPageByState(State::BOOKSHELF);
    g_app.DrawCurrentPage();
  }

  PERF_MARK("setup.boot_ready");
  reader::hal::LogBootTiming("boot_ready");
#if READER_PERF
  reader::core::RunSdBenchmark(g_app.settings().body_font_path);
#endif
  // From here on only `state_machine` touches the SD card and glyph caches.
  g_app.EnableBackgroundWork();
  reader::hal::ArmSdTaskInvariant(true);
}

void loop() {
  M5.update();
  g_app.PollHardwareInput();
  reader::app::PollSerialCommands();
  delay(10);
}
