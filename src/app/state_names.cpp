#include "app/state.h"

const char* StateName(State state) {
  switch (state) {
    case State::BOOKSHELF: return "BOOKSHELF";
    case State::READER: return "READER";
    case State::READER_MENU: return "READER_MENU";
    case State::READER_JUMP_MENU: return "READER_JUMP_MENU";
    case State::READER_TOC: return "READER_TOC";
    case State::TRANSFER: return "TRANSFER";
    case State::SETTING: return "SETTING";
    case State::READING_SETTINGS: return "READING_SETTINGS";
    case State::CONTROLS_SETTINGS: return "CONTROLS_SETTINGS";
    case State::TAP_ZONES: return "TAP_ZONES";
    case State::SWIPE_SETTINGS: return "SWIPE_SETTINGS";
    case State::USER_GUIDE: return "USER_GUIDE";
    case State::IMAGE_PICKER: return "IMAGE_PICKER";
    case State::SLEEP: return "SLEEP";
  }
  return "?";
}
