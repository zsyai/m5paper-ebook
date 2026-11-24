#include "app/app.h"

#include <M5Unified.h>
#include <SD.h>
#if READER_PERF
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include <cstdlib>
#include <string>

#include "app/state_machine.h"
#include "core/perf_trace.h"
#include "hal/input.h"
#include "hal/storage.h"

namespace reader {
namespace app {

App::App()
    : frame_(fonts_.ui_cache()),
      book_(storage_, fonts_, frame_),
      power_(storage_, fonts_, frame_, book_),
      ctx_(settings_, storage_, fonts_, frame_, book_, power_) {
  ctx_.draw_current_page = [this] { DrawCurrentPage(); };
  ctx_.redraw_region = [this](const ui::Rect& area, lgfx::epd_mode_t mode) {
    RedrawRegion(area, mode);
  };
  ctx_.sync_canvas_rotation = [this] { UpdateCanvasRotation(); };

  storage_.OnUnmount([this] {
    book_.PrepareForUnmount();
    fonts_.ReleaseSdHandles();
  });
}

void App::Init(QueueHandle_t* event_queue) {
  PERF_SCOPE("App::Init");
  ctx_.event_queue = event_queue;
  settings_.Load();
  book_.ApplyStyle(settings_);
  fonts_.Init(settings_);
  frame_.Init(fonts_.EffectiveUiFontPath(settings_), settings_.orientation,
              current_page_ && current_page_->followsReadingDirection());
  M5.Touch.setHoldThresh(600);
}

bool App::RestoreLastBookAtBoot() {
  if (!storage_.CheckBootToReader()) return false;
  const std::string last_book = storage_.LoadLastReadBook();
  if (last_book.empty() || !SD.exists(last_book.c_str())) return false;
  PERF_SCOPE("boot.restoreBook");
  if (!book_.Open(last_book, &settings_, /*show_loading=*/false)) return false;
  SetPageByState(State::READER);
  set_current_state(State::READER);
  DrawCurrentPage();
  return true;
}

void App::PollHardwareInput() {
  const uint32_t now = millis();
  if (M5.BtnA.wasClicked()) PostEvent(Event::Wheel(-1, now));
  if (M5.BtnC.wasClicked()) PostEvent(Event::Wheel(+1, now));
  if (M5.BtnB.wasClicked())
    PostEvent(Event::Simple(EventType::kWheelPress, now));

  auto detail = M5.Touch.getDetail();
  if (detail.wasHold()) {
    PostEvent(Event::LongPress(detail.base_x, detail.base_y, now));
  } else if (detail.wasClicked()) {
    PostEvent(Event::Tap(detail.x, detail.y, now));
  } else if (detail.wasFlicked()) {
    const int dx = detail.x - detail.base_x;
    const int dy = detail.y - detail.base_y;
    if (std::abs(dx) >= hal::kMinSwipeDistance ||
        std::abs(dy) >= hal::kMinSwipeDistance) {
      PostEvent(Event::Swipe(dx, dy, now));
    } else {
      PostEvent(Event::Tap(detail.base_x, detail.base_y, now));
    }
  } else if (detail.wasDragged()) {
    const int dx = detail.x - detail.base_x;
    const int dy = detail.y - detail.base_y;
    if (std::abs(dx) >= hal::kMinSwipeDistance ||
        std::abs(dy) >= hal::kMinSwipeDistance) {
      PostEvent(Event::Swipe(dx, dy, now));
    }
  }
}

void App::UpdateCanvasRotation() {
  const bool rotated =
      current_page_ != nullptr && current_page_->followsReadingDirection();
  frame_.UpdateRotation(settings_.orientation, rotated);
}

void App::SetPage(Page* page) {
  if (current_page_ != nullptr) {
    current_page_->exit(ctx_);
  }
  current_page_ = page;
  UpdateCanvasRotation();
  if (current_page_ != nullptr) {
    current_page_->enter(ctx_);
  }
}

void App::SetPageByState(State state) {
  PERF_SCOPE("App::SetPageByState");
  switch (state) {
    case State::BOOKSHELF:
      SetPage(&bookshelf_page_);
      break;
    case State::SETTING:
      SetPage(&system_settings_page_);
      break;
    case State::READING_SETTINGS:
      SetPage(&layout_settings_page_);
      break;
    case State::CONTROLS_SETTINGS:
      SetPage(&controls_settings_page_);
      break;
    case State::TAP_ZONES:
      SetPage(&tap_zones_page_);
      break;
    case State::SWIPE_SETTINGS:
      SetPage(&swipe_settings_page_);
      break;
    case State::READER:
      SetPage(&reader_page_);
      break;
    case State::READER_MENU:
      SetPage(&reader_menu_page_);
      break;
    case State::READER_JUMP_MENU:
      SetPage(&jump_page_);
      break;
    case State::READER_TOC:
      SetPage(&toc_page_);
      break;
    case State::TRANSFER:
      SetPage(&transfer_page_);
      break;
    case State::USER_GUIDE:
      SetPage(&user_guide_page_);
      break;
    case State::IMAGE_PICKER:
      SetPage(&image_picker_page_);
      break;
    case State::SLEEP:
      SetPage(&sleep_page_);
      break;
    default:
      SetPage(nullptr);
      break;
  }
}

void App::DrawCurrentPage() {
  PERF_SCOPE("App::DrawCurrentPage");
  if (hal::SdErrorPending()) return;
  if (current_page_ != nullptr) current_page_->show(ctx_);
}

void App::RedrawRegion(const ui::Rect& area, lgfx::epd_mode_t mode) {
  if (current_page_ == nullptr || hal::SdErrorPending()) return;
  current_page_->draw(ctx_, frame_.canvas());
  frame_.PushRegion(area, mode);
}

bool App::HasBackgroundWork() const {
  if (!background_enabled_ || get_current_state() == State::TRANSFER) {
    return false;
  }
  return book_.HasBackgroundWork() || fonts_.NeedsBackgroundBuild();
}

void App::BackgroundStep() {
  if (get_current_state() == State::TRANSFER) return;
  if (!book_.HasBackgroundWork()) {
    fonts_.StepBackgroundBuild(kBackgroundStepMs);
    return;
  }
  if (!book_.StepBackgroundWork(kBackgroundStepMs)) return;
  PostEvent(Event::Simple(EventType::kIndexReady, millis()));
}

void App::HandleSdError() {
  if (!storage_.ShouldRemountOnError()) return;
  log_w("[sd] card not responding, remounting");
  RemountStorage();
}

bool App::RemountStorage() {
  if (!storage_.Remount()) {
    frame_.ShowPromptBox("未检测到TF卡，插入后点击屏幕重试");
    hal::TakeSdError();
    return false;
  }
  const bool reopened = book_.ReopenAfterRemount(&settings_);
  hal::TakeSdError();
  if (!reopened) set_current_state(State::BOOKSHELF);
  frame_.RestartAllRefreshes();
  SetPageByState(get_current_state());
  DrawCurrentPage();
  return true;
}

#if READER_PERF
void App::LogDebugState(State top_state, int stack_depth) const {
  const std::string book = book_.IsOpen() ? book_.BookPath() : std::string("-");
  log_printf(
      "[E2E] state=%s stack=%d card=%d book=%s page=%d total=%d offset=%u "
      "progress=%.1f index_ready=%d orient=%d margin=%d font_size=%d "
      "spacing=%.1f tap_layout=%d sort=%d\n",
      StateName(top_state), stack_depth, storage_.card_present() ? 1 : 0,
      book.c_str(), book_.CurrentPageIndex(), book_.TotalPages(),
      static_cast<unsigned>(book_.CurrentOffset()), book_.Progress(),
      book_.IsIndexComplete() ? 1 : 0, static_cast<int>(settings_.orientation),
      settings_.margin, settings_.body_font_size, settings_.line_spacing,
      static_cast<int>(settings_.tap_layout),
      static_cast<int>(settings_.bookshelf_sort));
}

void App::LogDebugBooks() const {
  const std::vector<BookInfo>& books = bookshelf_page_.books();
  log_printf("[E2E] books_begin sort=%d count=%u\n",
             static_cast<int>(settings_.bookshelf_sort),
             static_cast<unsigned>(books.size()));
  for (size_t i = 0; i < books.size(); ++i) {
    const BookInfo& b = books[i];
    log_printf(
        "[E2E] book idx=%u opened=%d last=%d prog=%d seq=%u mtime=%u dir=%u\n",
        static_cast<unsigned>(i), book::HasBookBeenOpened(b) ? 1 : 0,
        b.isLastRead ? 1 : 0, b.progress,
        static_cast<unsigned>(b.last_read_seq),
        static_cast<unsigned>(b.import_time),
        static_cast<unsigned>(b.dir_order));
  }
  log_printf("[E2E] books_end\n");
}

void App::LogDebugMem() const {
  const unsigned index_bytes =
      book_.IsOpen()
          ? static_cast<unsigned>(book_.TotalPages()) * sizeof(uint32_t)
          : 0u;
  log_printf(
      "[E2E] mem heap_free=%u heap_min=%u psram_free=%u psram_min=%u "
      "ui_cache=%u page_cache=%u index_bytes=%u sm_stack_hwm=%u\n",
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(
          heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned>(fonts_.ui_cache().cachedBytes()),
      static_cast<unsigned>(fonts_.body_cache().cachedBytes()), index_bytes,
      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) *
                            sizeof(StackType_t)));
}

void App::DropCachesForTest() {
  (void)PageFontCache::BuiltinCharset();
  fonts_.body_cache().clear();
  fonts_.ui_cache().clear();
  log_printf("[E2E] caches_dropped=1\n");
}
#endif  // READER_PERF

}  // namespace app
}  // namespace reader
