#include "services/power_service.h"

#include <M5Unified.h>
#include <SD.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/diag_log.h"
#include "hal/battery.h"
#include "hal/input.h"
#include "i18n/i18n.h"
#include "ui/pages/image_picker_page.h"

namespace reader {
namespace services {
namespace {

constexpr gpio_num_t kWheelUpPin = GPIO_NUM_37;
constexpr gpio_num_t kWheelPushPin = GPIO_NUM_38;
constexpr gpio_num_t kWheelDownPin = GPIO_NUM_39;
constexpr gpio_num_t kMainPowerPin = GPIO_NUM_2;
// Light sleep longer than 24 h transitions to deep sleep (power off).
constexpr uint64_t kMaxLightSleepUs = 24ULL * 60 * 60 * 1000000;
// Hourly RAM-only battery check while in light sleep.
constexpr uint64_t kBatteryCheckUs = 60ULL * 60 * 1000000;
constexpr int kMaxSleepSamples = 48;

bool AnyWheelPressed() {
  return digitalRead(kWheelUpPin) == LOW || digitalRead(kWheelPushPin) == LOW ||
         digitalRead(kWheelDownPin) == LOW;
}

void WaitWheelReleased() {
  const uint32_t start = millis();
  while (AnyWheelPressed() && millis() - start < 3000) delay(10);
}

void AppendSleepLog(const char* event, uint32_t minutes, int mv) {
  core::DiagLog("sleep_log.txt", minutes, "%s bat=%d%% %dmV", event,
                hal::BatteryPercent(mv), mv);
}

void AppendSleepLog(const char* event) {
  AppendSleepLog(event, core::UptimeMinutes(), hal::ReadBatteryMv());
}

}  // namespace

PowerService::PowerService(StorageService& storage, FontService& fonts,
                           FrameService& frame, BookSession& book)
    : storage_(storage), fonts_(fonts), frame_(frame), book_(book) {}

int PowerService::BatteryLevel() const {
  return hal::BatteryPercent(hal::ReadBatteryMv());
}

bool PowerService::DrawSleepScreen(core::Settings* settings, bool light,
                                   bool from_reader) {
  LGFX_Sprite& canvas = frame_.canvas();
  const std::string& lock_path = settings->lock_screen_path;
  bool drawn = false;
  if (settings->lock_screen_enabled && !lock_path.empty() &&
      SD.exists(lock_path.c_str())) {
    if (from_reader) {
      book_.RefreshLayoutIfNeeded(settings);
      frame_.UpdateRotation(settings->orientation,
                            /*follows_reading_dir=*/true);
      book_.RenderCurrentPage();
    }
    frame_.UpdateRotation(settings->orientation, /*follows_reading_dir=*/false);
    drawn = ui::DrawCenteredImage(canvas, lock_path);
  } else {
    frame_.UpdateRotation(settings->orientation, /*follows_reading_dir=*/false);
  }

  if (!drawn) {
    canvas.fillScreen(TFT_WHITE);
    ui::UiPainter& p = frame_.painter();
    const std::string prompt1 = TR("系统已进入休眠");
    const std::string prompt2 =
        light ? TR("按下或拨动滚轮即可唤醒") : TR("向内长按滚轮键2秒即可唤醒");
    p.Prepare(prompt1 + prompt2);
    const int font_size = fonts_.ui_font_size();
    const int screen_w = canvas.width();
    const int prompt1_y = canvas.height() / 2 - font_size;
    p.Text(prompt1, screen_w / 2 - p.Width(prompt1) / 2, prompt1_y);
    p.Text(prompt2, screen_w / 2 - p.Width(prompt2) / 2,
           prompt1_y + 2 * font_size);
  }

  frame_.PushCanvas(lgfx::epd_mode_t::epd_quality);
  return drawn;
}

bool PowerService::RunSleep(core::Settings* settings, bool from_reader,
                            QueueHandle_t event_queue) {
  bool wallpaper_drawn = false;
  if (settings->sleep_mode == core::SleepMode::kLight &&
      LightSleep(settings, from_reader, event_queue, &wallpaper_drawn)) {
    return true;
  }
  DeepSleep(settings, from_reader, wallpaper_drawn);
  return false;
}

bool PowerService::LightSleep(core::Settings* settings, bool from_reader,
                              QueueHandle_t event_queue,
                              bool* wallpaper_drawn) {
  const int rest_mv = hal::ReadBatteryMv();
  *wallpaper_drawn = DrawSleepScreen(settings, /*light=*/true, from_reader);
  fonts_.CancelBackgroundBuilds();
  if (from_reader) book_.SaveConfig();
  M5.Display.waitDisplay();
  WaitWheelReleased();
  AppendSleepLog("light-sleep", core::UptimeMinutes(), rest_mv);
  M5.Display.sleep();

  gpio_hold_en(kMainPowerPin);
  gpio_wakeup_enable(kWheelUpPin, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(kWheelPushPin, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(kWheelDownPin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  log_i("[sleep] light sleep");

  struct Sample {
    uint32_t minutes;
    int mv;
  };
  static Sample samples[kMaxSleepSamples];
  int sample_count = 0;
  uint64_t slept_us = 0;
  esp_sleep_wakeup_cause_t cause = ESP_SLEEP_WAKEUP_UNDEFINED;
  while (true) {
    const uint64_t slice =
        std::min<uint64_t>(kBatteryCheckUs, kMaxLightSleepUs - slept_us);
    esp_sleep_enable_timer_wakeup(slice);
    esp_light_sleep_start();
    cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_TIMER) break;
    slept_us += slice;
    if (sample_count < kMaxSleepSamples) {
      samples[sample_count++] = {core::UptimeMinutes(), hal::ReadBatteryMv()};
    }
    if (slept_us >= kMaxLightSleepUs) break;
  }
  const int wake_mv = hal::ReadBatteryMv();
  const uint32_t wake_minutes = core::UptimeMinutes();

  gpio_wakeup_disable(kWheelUpPin);
  gpio_wakeup_disable(kWheelPushPin);
  gpio_wakeup_disable(kWheelDownPin);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  gpio_hold_dis(kMainPowerPin);
  M5.Display.wakeup();
  const bool timed_out = cause == ESP_SLEEP_WAKEUP_TIMER;
  for (int i = 0; i < sample_count; ++i) {
    AppendSleepLog("light-check", samples[i].minutes, samples[i].mv);
  }
  AppendSleepLog(timed_out ? "light-timeout" : "wake", wake_minutes, wake_mv);
  log_i("[sleep] woke, cause=%d after %d samples", static_cast<int>(cause),
        sample_count);
  if (timed_out) return false;

  WaitWheelReleased();
  delay(100);
  if (event_queue != nullptr) xQueueReset(event_queue);

  frame_.RestartAllRefreshes();
  return true;
}

void PowerService::DeepSleep(core::Settings* settings, bool from_reader,
                             bool wallpaper_drawn) {
  AppendSleepLog("deep-sleep");
  if (!wallpaper_drawn) {
    DrawSleepScreen(settings, /*light=*/false, from_reader);
  }

  esp_sleep_enable_ext0_wakeup(kWheelPushPin, 0);
  book_.CancelBackgroundWork();
  fonts_.CancelBackgroundBuilds();
  storage_.SetBootToReader(from_reader);
  if (from_reader) {
    book_.SaveConfig();
  }

  delay(500);
  M5.Display.waitDisplay();
  while (digitalRead(kWheelPushPin) == LOW) {
    delay(10);
  }
  log_i("[sleep] deep sleep, wakeup on GPIO%d low",
        static_cast<int>(kWheelPushPin));
  esp_deep_sleep_start();
}

}  // namespace services
}  // namespace reader
