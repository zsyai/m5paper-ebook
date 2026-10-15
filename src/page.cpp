#include "page.h"

#include "ui_manager.h"

extern UIManager ui;

void Page::show() {
  draw(ui.getCanvas());
  ui.pushUiFrame();
}
