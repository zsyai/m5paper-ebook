#include "hal/power.h"

#include <esp32-hal-log.h>
#include <esp_private/esp_clk.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_struct.h>
#include <soc/rtc.h>

namespace reader {
namespace hal {

namespace {

constexpr int kMainPowerPin = 2;  // M5EPD_MAIN_PWR_PIN

// RTC slow clock ticks when the first app constructor ran.
uint64_t g_early_ticks = 0;

uint32_t TicksToUs(uint64_t ticks) {
  return static_cast<uint32_t>(
      rtc_time_slowclk_to_us(ticks, esp_clk_slowclk_cal_get()));
}

// Runs before other constructors and before app_main(), i.e. as early as
// application code can run, and latches the main power there instead of
// waiting for M5.begin(). On battery the wheel must stay pressed until the
// latch is set, so the earlier it is, the shorter the press.
__attribute__((constructor(101))) void EarlyBoot() {
  g_early_ticks = rtc_time_get();
  esp_rom_gpio_pad_select_gpio(kMainPowerPin);
  GPIO.out_w1ts = 1u << kMainPowerPin;
  GPIO.enable_w1ts = 1u << kMainPowerPin;
}

}  // namespace

uint32_t UsSincePowerOn() { return TicksToUs(rtc_time_get()); }

void LogBootTiming(const char* tag) {
#if READER_PERF
  const bool driven = (GPIO.enable >> kMainPowerPin) & 1;
  const bool high = (GPIO.out >> kMainPowerPin) & 1;
  log_printf("[BOOT] %s early_app=%u us now=%u us gpio2=%s\n", tag,
             TicksToUs(g_early_ticks), UsSincePowerOn(),
             driven ? (high ? "out-high" : "out-low") : "input");
#else
  (void)tag;
#endif
}

}  // namespace hal
}  // namespace reader
