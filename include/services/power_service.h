#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_POWER_SERVICE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_POWER_SERVICE_H_

// Owns battery voltage-to-percent reporting, sleep-screen rendering, and
// light/deep sleep peripheral coordination.

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "core/settings.h"
#include "services/book_session.h"
#include "services/font_service.h"
#include "services/frame_service.h"
#include "services/storage_service.h"

namespace reader {
namespace services {

class PowerService {
 public:
  PowerService(StorageService& storage, FontService& fonts, FrameService& frame,
               BookSession& book);

  int BatteryLevel() const;

  // Enters light or deep sleep per `settings->sleep_mode`. Deep sleep powers
  // off and never returns; light sleep returns true when woken by the wheel.
  bool RunSleep(core::Settings* settings, bool from_reader,
                QueueHandle_t event_queue);

 private:
  bool DrawSleepScreen(core::Settings* settings, bool light, bool from_reader);
  bool LightSleep(core::Settings* settings, bool from_reader,
                  QueueHandle_t event_queue, bool* wallpaper_drawn);
  void DeepSleep(core::Settings* settings, bool from_reader,
                 bool wallpaper_drawn);

  StorageService& storage_;
  FontService& fonts_;
  FrameService& frame_;
  BookSession& book_;
};

}  // namespace services
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_POWER_SERVICE_H_
