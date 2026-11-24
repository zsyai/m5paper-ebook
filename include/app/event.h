#pragma once

// The one event type that pages handle.
//
// Input events come from loopTask (touch, wheel) through the app queue;
// system events (index ready, SD error) are posted by whoever notices them;
// the tick is delivered by the app loop whenever it is idle. Pages ignore
// the kinds they do not care about and return their own state.

#include <cstdint>

namespace reader {
namespace app {

enum class EventType : uint8_t {
  kTap,         // tap: x, y in the current page's canvas coordinates
  kLongPress,   // tap: x, y held past the hold threshold, in canvas coordinates
  kSwipe,       // swipe: dx, dy in canvas coordinates (see hal::ClassifySwipe)
  kWheel,       // wheel: delta, +1 = next / down / right on the page
  kWheelPress,  // the wheel pushed in
  kTick,        // idle, at most every ~200 ms (auto page turn, timeouts)
  kIndexReady,  // the open book's page index finished in the background
  kSdError,     // an SD read or write failed; handled by the app, not pages
  kWoke,        // the device woke from light sleep (SleepPage)
  kDebug,       // perf-build serial E2E query/inspection command
};

struct Event {
  EventType type;
  uint32_t timestamp_ms;
  union {
    struct {
      int16_t x;
      int16_t y;
    } tap;
    struct {
      int16_t dx;
      int16_t dy;
    } swipe;
    struct {
      int8_t delta;
    } wheel;
    uint8_t debug_op;
  };

  static Event Tap(int16_t x, int16_t y, uint32_t now);
  static Event LongPress(int16_t x, int16_t y, uint32_t now);
  static Event Swipe(int16_t dx, int16_t dy, uint32_t now);
  static Event Wheel(int8_t delta, uint32_t now);
  static Event Debug(uint8_t op, uint32_t now);
  static Event Simple(EventType type, uint32_t now);
};

inline Event Event::Simple(EventType type, uint32_t now) {
  Event e{};
  e.type = type;
  e.timestamp_ms = now;
  return e;
}

inline Event Event::Tap(int16_t x, int16_t y, uint32_t now) {
  Event e = Simple(EventType::kTap, now);
  e.tap.x = x;
  e.tap.y = y;
  return e;
}

inline Event Event::LongPress(int16_t x, int16_t y, uint32_t now) {
  Event e = Simple(EventType::kLongPress, now);
  e.tap.x = x;
  e.tap.y = y;
  return e;
}

inline Event Event::Swipe(int16_t dx, int16_t dy, uint32_t now) {
  Event e = Simple(EventType::kSwipe, now);
  e.swipe.dx = dx;
  e.swipe.dy = dy;
  return e;
}

inline Event Event::Wheel(int8_t delta, uint32_t now) {
  Event e = Simple(EventType::kWheel, now);
  e.wheel.delta = delta;
  return e;
}

inline Event Event::Debug(uint8_t op, uint32_t now) {
  Event e = Simple(EventType::kDebug, now);
  e.debug_op = op;
  return e;
}

// Human-readable name for logs ("tap", "swipe", ...).
const char* EventName(EventType type);

}  // namespace app
}  // namespace reader
