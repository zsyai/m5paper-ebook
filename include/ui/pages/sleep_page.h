#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_SLEEP_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_SLEEP_PAGE_H_

// 休眠 page: delegates light/deep sleep execution to `PowerService` via
// `AppContext::RunSleep` and pops back on wake (`EventType::kWoke`).

#include "ui/page.h"

namespace reader {
namespace ui {

class SleepPage : public Page {
 public:
  void enter(app::AppContext& ctx) override;
  void draw(app::AppContext& ctx, LGFX_Sprite& canvas) override {}
  void show(app::AppContext& ctx) override;
  app::Nav handleEvent(const app::Event& event, app::AppContext& ctx) override;

 private:
  bool from_reader_ = false;
};

}  // namespace ui
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_SLEEP_PAGE_H_
