#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_STATE_MACHINE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_STATE_MACHINE_H_

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app/event.h"
#include "app/nav.h"
#include "app/state.h"

extern QueueHandle_t g_state_machine_queue;

void init_state_machine();
// Restarts navigation at `state` (boot); the page itself is set by the
// caller through `App::SetPageByState()`.
void set_current_state(State state);
// The page on top of the navigation stack. While a page is being entered
// this is still the page being left, so enter() can tell where it came from.
State get_current_state();
void state_machine_task(void* pvParameters);
// Queues an event for the app loop. Safe from any task; returns false if
// the queue is full (the event is dropped).
bool PostEvent(const reader::app::Event& event);

namespace reader {
namespace app {

#if READER_PERF
// Reads newline-delimited E2E commands from UART0 (`Serial`) on `loopTask` and
// enqueues them onto `g_state_machine_queue` so input injection and state or
// screenshot inspection serialize cleanly on `state_machine_task`.
void PollSerialCommands();
#else
inline void PollSerialCommands() {}
#endif

}  // namespace app
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_STATE_MACHINE_H_
