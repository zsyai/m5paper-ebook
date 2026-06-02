#pragma once

#include <M5GFX.h>
#include "state_machine.h"

class Page {
 public:
  virtual ~Page() = default;
  virtual void enter() = 0;
  virtual void exit() = 0;
  virtual void draw(LGFX_Sprite& canvas) = 0;
  virtual State handleEvent(const SystemMessage_t& msg) = 0;
};
