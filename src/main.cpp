#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "ui_manager.h"
#include "state_machine.h"
#include "core/perf_trace.h"
#include "core/sd_bench.h"

#define SD_SPI_SCK_PIN 14
#define SD_SPI_MISO_PIN 13
#define SD_SPI_MOSI_PIN 12
#define SD_SPI_CS_PIN 4

UIManager ui;

void setup() {
  PERF_SCOPE("setup");
  // esp_reset_reason_t: 1 = power-on, 8 = deep sleep, 3 = software.
  // esp_sleep_wakeup_cause_t: 0 = not a sleep wakeup, 2 = ext0 (wheel).
  log_i("[boot] reset_reason=%d wakeup_cause=%d", (int)esp_reset_reason(),
        (int)esp_sleep_get_wakeup_cause());
  auto cfg = M5.config();
  // Boot-time savings (see perf baseline): the first frame is a full GC16
  // refresh anyway, so skip the panel clear, and skip probing peripherals
  // that M5Paper does not have. The RTC stays enabled.
  cfg.clear_display = false;
  cfg.internal_imu = false;
  cfg.internal_mic = false;
  cfg.internal_spk = false;
  {
    PERF_SCOPE("M5.begin");
    M5.begin(cfg);
  }
  PERF_MARK("setup.after_m5_begin");

  // Initialize SPI for SD card
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);

  bool sd_ok;
  {
    PERF_SCOPE("SD.begin");
    // Long-lived descriptors: body and UI font sources (a TTF plus its
    // glyph cache each) and a font cache being written. The default limit
    // of 5 left no descriptors for bookshelf and /fonts scans.
    constexpr uint8_t kSdMaxOpenFiles = 16;
    sd_ok = SD.begin(SD_SPI_CS_PIN, SPI, 25000000, "/sd", kSdMaxOpenFiles);
  }
  if (!sd_ok) {
    Serial.println("SD Card initialization failed!");
    ui.setTFCardPresent(false);
  } else {
    Serial.println("SD Card initialized successfully.");
    ui.setTFCardPresent(true);
    if (!SD.exists("/configs")) {
      if (SD.mkdir("/configs")) {
        Serial.println("Created /configs directory.");
      } else {
        Serial.println("Failed to create /configs directory.");
      }
    }
  }

  ui.init();
  init_state_machine();
  PERF_MARK("setup.ui_ready");
  
  bool restore_reading = false;
  if (ui.checkBootToReaderFlag()) {
    std::string last_book = ui.getLastReadBook();
    if (!last_book.empty() && SD.exists(last_book.c_str())) {
      PERF_SCOPE("boot.restoreBook");
      ui.openBook(last_book, false);
      ui.setPageByState(State::READER);
      set_current_state(State::READER);
      ui.drawCurrentPage();
      restore_reading = true;
    }
  }
  
  if (!restore_reading) {
    PERF_SCOPE("boot.bookshelf");
    ui.setPageByState(State::BOOKSHELF);
    ui.drawCurrentPage();
  }
  PERF_MARK("setup.boot_ready");
#if READER_PERF
  // Perf builds only, after the first frame: SD latency / throughput numbers.
  reader::core::RunSdBenchmark(ui.getFontPath());
#endif
  // From here on only the state machine task touches the SD card and caches.
  ui.enableBackgroundWork();
}

void loop() {
  M5.update();
  ui.handleInput();
  delay(10);
}
