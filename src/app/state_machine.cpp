#include "app/state_machine.h"

#include <M5Unified.h>
#if READER_PERF
#include <cstdio>
#include <string>
#endif

#include "app/app.h"
#if READER_PERF
#include "core/debug_cmd.h"
#endif
#include "hal/storage.h"

using reader::app::Event;
using reader::app::EventType;
using reader::app::Nav;
using reader::app::NavStack;

QueueHandle_t g_state_machine_queue = NULL;
static NavStack s_nav;

namespace reader {
namespace app {

const char* EventName(EventType type) {
  switch (type) {
    case EventType::kTap:
      return "tap";
    case EventType::kLongPress:
      return "long_press";
    case EventType::kSwipe:
      return "swipe";
    case EventType::kWheel:
      return "wheel";
    case EventType::kWheelPress:
      return "wheel_press";
    case EventType::kTick:
      return "tick";
    case EventType::kIndexReady:
      return "index_ready";
    case EventType::kSdError:
      return "sd_error";
    case EventType::kWoke:
      return "woke";
    case EventType::kDebug:
      return "debug";
  }
  return "?";
}

#if READER_PERF
namespace {

bool EnqueueSerialEvent(const Event& event) {
  if (g_state_machine_queue == NULL) return false;
  return xQueueSend(g_state_machine_queue, &event, pdMS_TO_TICKS(2000)) ==
         pdTRUE;
}

std::string SanitizeSerialLogLine(const char* line) {
  std::string out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(line);
       *p != '\0' && out.size() < 96; ++p) {
    if (*p >= 0x20 && *p <= 0x7E && *p != '"' && *p != '\\') {
      out.push_back(static_cast<char>(*p));
    } else {
      char esc[5];
      std::snprintf(esc, sizeof(esc), "\\x%02X", *p);
      out.append(esc);
    }
  }
  return out;
}

void ExecuteSerialLine(const char* line) {
  const core::DebugCommand cmd = core::ParseDebugCommand(line);
  const uint32_t now = millis();
  switch (cmd.kind) {
    case core::DebugCommand::Kind::kEmpty:
      return;
    case core::DebugCommand::Kind::kInvalid: {
      const std::string clean = SanitizeSerialLogLine(line);
      log_printf("[E2E] error=invalid_command line=\"%s\"\n", clean.c_str());
      return;
    }
    case core::DebugCommand::Kind::kTap:
      EnqueueSerialEvent(Event::Tap(cmd.a, cmd.b, now));
      return;
    case core::DebugCommand::Kind::kLongPress:
      EnqueueSerialEvent(Event::LongPress(cmd.a, cmd.b, now));
      return;
    case core::DebugCommand::Kind::kSwipe:
      EnqueueSerialEvent(Event::Swipe(cmd.a, cmd.b, now));
      return;
    case core::DebugCommand::Kind::kWheel: {
      const int8_t step = (cmd.a > 0) ? 1 : -1;
      const int count = (cmd.a > 0) ? cmd.a : -cmd.a;
      for (int i = 0; i < count; ++i) {
        EnqueueSerialEvent(Event::Wheel(step, millis()));
      }
      return;
    }
    case core::DebugCommand::Kind::kWheelPress:
      EnqueueSerialEvent(Event::Simple(EventType::kWheelPress, now));
      return;
    case core::DebugCommand::Kind::kDebugOp:
      EnqueueSerialEvent(Event::Debug(static_cast<uint8_t>(cmd.op), now));
      return;
  }
}

void HandleDebugOp(uint8_t raw_op) {
  switch (static_cast<core::DebugOp>(raw_op)) {
    case core::DebugOp::kPing:
      log_printf("[E2E] pong up_ms=%lu\n",
                 static_cast<unsigned long>(millis()));
      break;
    case core::DebugOp::kState:
      g_app.LogDebugState(s_nav.top(), s_nav.size());
      break;
    case core::DebugOp::kMem:
      g_app.LogDebugMem();
      break;
    case core::DebugOp::kScreenshot:
      g_app.DumpScreenshotPbm();
      break;
    case core::DebugOp::kDropCaches:
      g_app.DropCachesForTest();
      break;
    case core::DebugOp::kBooks:
      g_app.LogDebugBooks();
      break;
  }
}

}  // namespace

void PollSerialCommands() {
  static char s_line[128];
  static size_t s_len = 0;
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\r') continue;
    if (ch == '\n') {
      s_line[s_len] = '\0';
      s_len = 0;
      ExecuteSerialLine(s_line);
      return;
    }
    if (s_len + 1 < sizeof(s_line)) {
      s_line[s_len++] = static_cast<char>(ch);
    }
  }
}
#endif  // READER_PERF

}  // namespace app
}  // namespace reader

void init_state_machine() {
  g_state_machine_queue = xQueueCreate(32, sizeof(Event));
  if (g_state_machine_queue == NULL) {
    log_e("Failed to create queue");
    return;
  }
  reader::hal::SetSdErrorListener(
      [] { PostEvent(Event::Simple(EventType::kSdError, millis())); });

  xTaskCreate(state_machine_task, "state_machine", 8192, NULL, 1, NULL);
}

void set_current_state(State state) { s_nav.Reset(state); }

State get_current_state() { return s_nav.top(); }

namespace {

void Dispatch(const Event& event) {
  Page* current_page = g_app.current_page();
  if (current_page == nullptr) return;
  const Nav nav = current_page->handleEvent(event, g_app.ctx());
  const State from = s_nav.top();
  NavStack next = s_nav;
  if (!next.Apply(nav)) return;
  const State to = next.top();
  log_i("[STATE] %s -> %s (%s)", StateName(from), StateName(to),
        reader::app::EventName(event.type));
  // Pages read get_current_state() in enter() to see where they were opened
  // from, so the stack advances right after SetPageByState().
  g_app.SetPageByState(to);
  s_nav = next;
  g_app.DrawCurrentPage();
}

}  // namespace

void state_machine_task(void* /*pvParameters*/) {
  while (!g_app.IsBackgroundWorkEnabled()) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  const TickType_t kIdleWaitTicks = pdMS_TO_TICKS(200);
  Event event;
  while (1) {
    const bool background = g_app.HasBackgroundWork();
    if (xQueueReceive(g_state_machine_queue, &event,
                      background ? 0 : kIdleWaitTicks) != pdTRUE) {
      if (g_app.storage().card_present()) {
        Dispatch(Event::Simple(EventType::kTick, millis()));
      }
      if (background) {
        g_app.BackgroundStep();
        vTaskDelay(1);
      }
      continue;
    }
#if READER_PERF
    if (event.type == EventType::kDebug) {
      reader::app::HandleDebugOp(event.debug_op);
      continue;
    }
#endif
    if (event.type == EventType::kSdError) {
      g_app.HandleSdError();
      continue;
    }
    if (!g_app.storage().card_present()) {
      if (event.type != EventType::kIndexReady) g_app.RetryStorage();
      continue;
    }
    g_app.MapInputToPage(&event);
    Dispatch(event);
  }
}

bool PostEvent(const Event& event) {
  if (g_state_machine_queue == NULL) return false;
  return xQueueSend(g_state_machine_queue, &event, 0) == pdTRUE;
}
