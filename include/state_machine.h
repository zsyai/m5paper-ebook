#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_STATE_MACHINE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_STATE_MACHINE_H_

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum class State {
    HOME,
    BOOKSHELF,
    READER,
    READER_MENU,
    READER_JUMP_MENU,
    TRANSFER,
    SETTING,
    CONTINUE,
    USER_GUIDE,
    IMAGE_PICKER,
    SLEEP
};

enum class MessageType {
    TOUCH,
    WHEEL,
    TIMER,
    SYSTEM
};

struct TouchData {
    int16_t x;
    int16_t y;
};

struct WheelData {
    int8_t delta;  // +1 or -1
};

union MessageData {
    TouchData touch;
    WheelData wheel;
    int32_t sys_code;
};

struct SystemMessage_t {
    MessageType type;
    uint32_t timestamp;
    MessageData data;
};

extern QueueHandle_t g_state_machine_queue;

void init_state_machine();
void set_current_state(State state);
void state_machine_task(void *pvParameters);
bool send_message(const SystemMessage_t& msg);

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_STATE_MACHINE_H_
