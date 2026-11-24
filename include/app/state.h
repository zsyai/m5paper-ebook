#pragma once

// Screens of the reader. One State per page; the app keeps a stack of them
// (see app/nav_stack.h) so sub-pages can go back to wherever they came from.

enum class State {
  BOOKSHELF,
  READER,
  READER_MENU,
  READER_JUMP_MENU,
  READER_TOC,
  TRANSFER,
  SETTING,            // 系统设置
  READING_SETTINGS,   // 排版设置
  CONTROLS_SETTINGS,  // 操作设置
  TAP_ZONES,          // 操作设置 > 点击区域
  SWIPE_SETTINGS,     // 操作设置 > 滑动操作
  USER_GUIDE,
  IMAGE_PICKER,
  SLEEP
};

// Name for logs, e.g. "READER_MENU".
const char* StateName(State state);
