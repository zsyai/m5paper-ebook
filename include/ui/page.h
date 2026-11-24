#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGE_H_

#include <M5GFX.h>

#include "app/event.h"
#include "app/nav.h"
#include "app/state_machine.h"

namespace reader {
namespace app {
struct AppContext;
}  // namespace app
}  // namespace reader

class Page {
 public:
  virtual ~Page() = default;
  virtual void enter(reader::app::AppContext& ctx) = 0;
  virtual void exit(reader::app::AppContext& ctx) {}
  virtual void draw(reader::app::AppContext& ctx, LGFX_Sprite& canvas) = 0;
  // Handles one event (input, tick, index ready) and returns the requested
  // navigation transition (`app/nav.h`).
  virtual reader::app::Nav handleEvent(const reader::app::Event& event,
                                       reader::app::AppContext& ctx) = 0;

  // Draws the page into `ctx.canvas()` and pushes it with the UI refresh
  // policy (`FrameService::PushUiFrame`); reader pages override as needed.
  virtual void show(reader::app::AppContext& ctx);

  // True for pages drawn in the reading direction (the reading view and its
  // menu). Every other page is drawn in portrait.
  virtual bool followsReadingDirection() const { return false; }
};

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGE_H_
