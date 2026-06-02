#include "continue_page.h"

#include <M5Unified.h>

void ContinuePage::init() {}

void ContinuePage::draw() {
  M5.Display.clear();
  M5.Display.setTextSize(3);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_BLACK);

  M5.Display.drawString("Continue", M5.Display.width() / 2,
                        M5.Display.height() / 2);

  // Draw Back button at top right
  M5.Display.drawRect(M5.Display.width() - 80, 5, 70, 30, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("Back", M5.Display.width() - 45, 20);
}
