#include "ui/page.h"

#include "app/app_context.h"

void Page::show(reader::app::AppContext& ctx) {
  draw(ctx, ctx.canvas());
  ctx.frame.PushUiFrame();
}
