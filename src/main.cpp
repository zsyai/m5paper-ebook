#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "ui_manager.h"
#include "state_machine.h"

#define SD_SPI_SCK_PIN 14
#define SD_SPI_MISO_PIN 13
#define SD_SPI_MOSI_PIN 12
#define SD_SPI_CS_PIN 4

UIManager ui;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // Initialize SPI for SD card
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);

  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    Serial.println("SD Card initialization failed!");
  } else {
    Serial.println("SD Card initialized successfully.");
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
  
  bool restore_reading = false;
  if (ui.checkBootToReaderFlag()) {
    std::string last_book = ui.getLastReadBook();
    if (!last_book.empty() && SD.exists(last_book.c_str())) {
      ui.openBook(last_book, false);
      set_current_state(State::READER);
      restore_reading = true;
    }
  }
  
  if (!restore_reading) {
    ui.setPageByState(State::BOOKSHELF);
    ui.drawCurrentPage();
  }
}

void loop() {
  M5.update();
  ui.handleInput();
  delay(10);
}
