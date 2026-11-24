#include "ui/pages/sleep_page.h"

#include <M5Unified.h>

#include "app/app_context.h"

namespace reader {
namespace ui {

void SleepPage::enter(app::AppContext& /*ctx*/) {
  from_reader_ = get_current_state() == State::READER_MENU;
}

void SleepPage::show(app::AppContext& ctx) {
  if (ctx.RunSleep(from_reader_)) {
    PostEvent(app::Event::Simple(app::EventType::kWoke, millis()));
  }
}

app::Nav SleepPage::handleEvent(const app::Event& event,
                                app::AppContext& /*ctx*/) {
  if (event.type == app::EventType::kWoke) return app::Nav::Back();
  return app::Nav::Stay();
}

}  // namespace ui
}  // namespace reader
