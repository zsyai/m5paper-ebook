#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_APP_APP_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_APP_APP_H_

// Top-level application owner: wires `StorageService`, `FontService`,
// `FrameService`, `BookSession`, `PowerService`, and the 13 `Page` instances
// behind `AppContext`.

#include <M5GFX.h>

#include <atomic>

#include "app/app_context.h"
#include "app/event.h"
#include "app/state.h"
#include "core/settings.h"
#include "services/book_session.h"
#include "services/font_service.h"
#include "services/frame_service.h"
#include "services/power_service.h"
#include "services/storage_service.h"
#include "ui/page.h"
#include "ui/pages/bookshelf_page.h"
#include "ui/pages/controls_settings_page.h"
#include "ui/pages/image_picker_page.h"
#include "ui/pages/layout_settings_page.h"
#include "ui/pages/reader_pages.h"
#include "ui/pages/sleep_page.h"
#include "ui/pages/system_settings_page.h"
#include "ui/pages/toc_page.h"
#include "ui/pages/transfer_page.h"
#include "ui/pages/user_guide_page.h"
#include "ui/rect.h"

namespace reader {
namespace app {

class App {
 public:
  static constexpr uint32_t kBackgroundStepMs = 40;

  App();

  void Init(QueueHandle_t* event_queue);
  bool RestoreLastBookAtBoot();

  // Polls hardware wheel/touch on `loopTask` and posts `Event`s to the queue.
  void PollHardwareInput();

  AppContext& ctx() { return ctx_; }
  const core::Settings& settings() const { return settings_; }
  services::StorageService& storage() { return storage_; }

  Page* current_page() { return current_page_; }
  void SetPage(Page* page);
  void SetPageByState(State state);
  void DrawCurrentPage();
  void RedrawRegion(const ui::Rect& area, lgfx::epd_mode_t mode);
  void MapInputToPage(Event* event) const { frame_.MapInputToCanvas(event); }

  void EnableBackgroundWork() { background_enabled_ = true; }
  bool IsBackgroundWorkEnabled() const { return background_enabled_.load(); }
  bool HasBackgroundWork() const;
  void BackgroundStep();

  void HandleSdError();
  void RetryStorage() { RemountStorage(); }

#if READER_PERF
  void LogDebugState(State top_state, int stack_depth) const;
  void LogDebugMem() const;
  void LogDebugBooks() const;
  void DropCachesForTest();
  void DumpScreenshotPbm() { frame_.DumpScreenshotPbm(); }
#endif

 private:
  void UpdateCanvasRotation();
  bool RemountStorage();

  std::atomic<bool> background_enabled_{false};
  core::Settings settings_;
  services::StorageService storage_;
  services::FontService fonts_;
  services::FrameService frame_;
  services::BookSession book_;
  services::PowerService power_;
  AppContext ctx_;

  ui::BookshelfPage bookshelf_page_;
  ui::SystemSettingsPage system_settings_page_;
  ui::LayoutSettingsPage layout_settings_page_;
  ui::ControlsSettingsPage controls_settings_page_;
  ui::TapZonesPage tap_zones_page_;
  ui::SwipeSettingsPage swipe_settings_page_;
  ui::SleepPage sleep_page_;
  ui::ReaderPage reader_page_;
  ui::ReaderMenuPage reader_menu_page_;
  ui::JumpPage jump_page_;
  ui::TocPage toc_page_;
  ui::TransferPage transfer_page_;
  ui::UserGuidePage user_guide_page_;
  ui::ImagePickerPage image_picker_page_;
  Page* current_page_ = nullptr;
};

}  // namespace app
}  // namespace reader

extern reader::app::App g_app;

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_APP_APP_H_
