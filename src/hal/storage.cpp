#include "hal/storage.h"

#include <SD.h>
#include <SPI.h>
#if READER_PERF && !READER_HOST_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "app/state_machine.h"
#endif

#include <atomic>

#include "core/perf_trace.h"

namespace reader {
namespace hal {

namespace {

constexpr uint8_t kSdCsPin = 4;
constexpr uint32_t kSdClockHz = 25000000;
// Long-lived descriptors: body and UI font sources (a TTF plus its glyph
// cache each) and a font cache being written. The default limit of 5 left
// no descriptors for bookshelf and /fonts scans.
constexpr uint8_t kSdMaxOpenFiles = 16;

std::atomic<bool> g_sd_error{false};
void (*g_sd_error_listener)() = nullptr;

}  // namespace

bool MountSd() {
  PERF_SCOPE("MountSd");
  // Some cards do not answer the SPI-mode initialization on the first try
  // (right after power-up, or after being pulled mid-transfer) or not at
  // 25 MHz at all; try again, slower, before giving up. Each attempt starts
  // from a clean driver state.
  static const uint32_t kClocks[] = {kSdClockHz, 10000000, 4000000};
  for (size_t i = 0; i < sizeof(kClocks) / sizeof(kClocks[0]); ++i) {
    if (i > 0) {
      SD.end();
      // A card left mid-transfer (pulled during a write, or a reset while
      // it was busy) may ignore the init commands until it has been
      // clocked with CS high for a while; the 74 clocks SD.begin() sends
      // are not always enough. Only a power cycle is certain, and on USB
      // the card never loses power.
      pinMode(kSdCsPin, OUTPUT);
      digitalWrite(kSdCsPin, HIGH);
      SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
      for (int n = 0; n < 512; ++n) SPI.transfer(0xFF);
      SPI.endTransaction();
      delay(100);
    }
    if (SD.begin(kSdCsPin, SPI, kClocks[i], "/sd", kSdMaxOpenFiles)) {
      if (!SD.exists("/configs")) SD.mkdir("/configs");
      log_i("[sd] mounted at %u kHz, type %d, %llu MB", kClocks[i] / 1000,
            (int)SD.cardType(), SD.cardSize() / (1024 * 1024));
      return true;
    }
    log_w("[sd] mount at %u kHz failed", kClocks[i] / 1000);
  }
  SD.end();
  return false;
}

void UnmountSd() { SD.end(); }

bool SdResponds() {
  if (SD.cardType() == CARD_NONE) return false;
  uint8_t sector[512];
  return SD.readRAW(sector, 0);
}

void ReportSdError() {
  if (!g_sd_error.exchange(true) && g_sd_error_listener) g_sd_error_listener();
}

bool TakeSdError() { return g_sd_error.exchange(false); }

bool SdErrorPending() { return g_sd_error.load(); }

void SetSdErrorListener(void (*listener)()) { g_sd_error_listener = listener; }

#if READER_PERF && !READER_HOST_TEST
namespace {
std::atomic<bool> g_sd_invariant_armed{false};
}  // namespace

void ArmSdTaskInvariant(bool armed) { g_sd_invariant_armed.store(armed); }

void CheckSdTaskInvariant(const char* op) {
  if (!g_sd_invariant_armed.load()) return;
  const char* task = pcTaskGetName(nullptr);
  if (task != nullptr && std::strcmp(task, "state_machine") == 0) return;
  const State state = get_current_state();
  if (state == State::TRANSFER && task != nullptr &&
      std::strcmp(task, "webserver_task") == 0) {
    return;
  }
  log_printf("[INVARIANT] SD access (%s) on task %s in state %s\n",
             op != nullptr ? op : "?", task != nullptr ? task : "?",
             StateName(state));
}
#endif

}  // namespace hal
}  // namespace reader
