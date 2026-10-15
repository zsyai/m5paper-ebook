#pragma once

#include <M5GFX.h>
#include "app/state_machine.h"

class Page {
 public:
  virtual ~Page() = default;
  virtual void enter() = 0;
  virtual void exit() = 0;
  virtual void draw(LGFX_Sprite& canvas) = 0;
  virtual State handleEvent(const SystemMessage_t& msg) = 0;

  // Draws the page and pushes it to the panel. The default calls draw() and
  // pushes with the UI refresh policy (UIManager::pushUiFrame); the reader
  // pages choose their own refresh modes.
  virtual void show();

  // Called when the background page index completes while this page is
  // shown, e.g. to replace "…" with page numbers.
  virtual void onIndexReady() {}

  // True for pages drawn in the reading direction (the reading view and
  // its menu). Every other page is drawn in portrait, also when it opens
  // over a landscape page.
  virtual bool followsReadingDirection() const { return false; }
};
