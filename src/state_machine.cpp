#include "state_machine.h"
#include <M5Unified.h>
#include "ui_manager.h"

QueueHandle_t g_state_machine_queue = NULL;
static State s_current_state = State::BOOKSHELF;

// Assume a global instance of UIManager exists in main.cpp
extern UIManager ui;

void init_state_machine() {
    g_state_machine_queue = xQueueCreate(10, sizeof(SystemMessage_t));
    if (g_state_machine_queue == NULL) {
        log_e("Failed to create queue");
        return;
    }
    
    xTaskCreate(
        state_machine_task,
        "state_machine",
        4096,
        NULL,
        1,
        NULL
    );
}

void set_current_state(State state) {
    s_current_state = state;
}



static bool check_back_button(int x, int y) {
    return (x >= M5.Display.width() - 80 && x <= M5.Display.width() - 10 &&
            y >= 5 && y <= 35);
}

static void handle_state_generic_page(const SystemMessage_t& msg) {
    if (msg.type == MessageType::TOUCH) {
        int x = msg.data.touch.x;
        int y = msg.data.touch.y;

        if (check_back_button(x, y)) {
            ui.setPageByState(State::BOOKSHELF);
            s_current_state = State::BOOKSHELF;
            ui.drawCurrentPage();
        }
    }
}



static void handle_state_reader(const SystemMessage_t& msg) {
    if (msg.type == MessageType::WHEEL) {
        int delta = msg.data.wheel.delta;
        if (delta > 0) {
            ui.nextPage();
        } else if (delta < 0) {
            ui.prevPage();
        }
    } else if (msg.type == MessageType::TOUCH) {
        int x = msg.data.touch.x;
        int y = msg.data.touch.y;

        int screen_w = M5.Display.width();
        if (x < screen_w / 3) {
            ui.prevPage();
        } else if (x > 2 * screen_w / 3) {
            ui.nextPage();
        } else {
            s_current_state = State::READER_MENU;
            ui.drawReaderMenu();
        }
    } else if (msg.type == MessageType::SYSTEM) {
        if (msg.data.sys_code == 1) { // Wheel press
            ui.drawReadingView(true);
        }
    }
}

static void handle_state_reader_menu(const SystemMessage_t& msg) {
    if (msg.type == MessageType::TOUCH) {
        int x = msg.data.touch.x;
        int y = msg.data.touch.y;

        int screen_w = M5.Display.width();
        int screen_h = M5.Display.height();
        int bar_h = 180; 
        int button_h = 90;
        int col_w = screen_w / 2; // For top bar

        if (y < bar_h) { // Top bar
            if (y < button_h) { // Row 1 (Buttons)
                if (x < col_w) { // "返回" button
                    ui.setPageByState(State::BOOKSHELF);
                    s_current_state = State::BOOKSHELF;
                    ui.setSelection(0);
                    ui.resetMenuUpdateCount();
                    ui.drawCurrentPage();
                }
            }
        } else if (y >= screen_h - 200) { // Bottom bar (200px high)
            if (ui.isReadingUserGuide()) {
                // In user guide, clicking anywhere in bottom bar closes the menu
                s_current_state = State::READER;
                ui.drawReadingView();
            } else if (y >= screen_h - 100) { // Row 2 (Buttons)
                int col_w2 = screen_w / 2; // 270px
                if (x < col_w2) { // "跳转" button
                    s_current_state = State::READER_JUMP_MENU;
                    ui.drawReaderJumpMenu("");
                } else { // "休眠" button
                    ui.enterDeepSleep(true);
                }
            } else { // Row 1 (Progress)
                // Close menu
                s_current_state = State::READER;
                ui.drawReadingView();
            }
        } else { // Middle of screen
            s_current_state = State::READER;
            ui.drawReadingView();
        }
    } else if (msg.type == MessageType::SYSTEM) {
        if (msg.data.sys_code == 1) { // Wheel press
            s_current_state = State::READER;
            ui.drawReadingView();
        }
    }
}

static void handle_state_reader_jump_menu(const SystemMessage_t& msg) {
    if (msg.type == MessageType::TOUCH) {
        int x = msg.data.touch.x;
        int y = msg.data.touch.y;

        int screen_w = M5.Display.width();
        int screen_h = M5.Display.height();
        
        int box_w = 340;
        int box_h = 400;
        int box_x = (screen_w - box_w) / 2;
        int box_y = (screen_h - box_h) / 2;

        if (x >= box_x && x <= box_x + box_w &&
            y >= box_y && y <= box_y + box_h) {
            
            int col = (x - box_x) / (box_w / 3);
            int row = (y - box_y) / (box_h / 4);
            
            int val = row * 3 + col + 1;
            
            if (val >= 1 && val <= 9) {
                ui.jumpInputText += std::to_string(val);
                ui.drawReaderJumpMenu(ui.jumpInputText);
            } else if (val == 10) {
                // Cancel (Bottom Left)
                ui.jumpInputText = "";
                s_current_state = State::READER;
                ui.drawReadingView();
            } else if (val == 11) {
                // 0 (Bottom Middle)
                ui.jumpInputText += "0";
                ui.drawReaderJumpMenu(ui.jumpInputText);
            } else if (val == 12) {
                // BS (Bottom Right)
                if (!ui.jumpInputText.empty()) {
                    ui.jumpInputText.pop_back();
                }
                ui.drawReaderJumpMenu(ui.jumpInputText);
            }
        } else {
            int btn_y = box_y + box_h + 10;
            int btn_h = 80;
            int btn_w = 160;
            
            int btn1_x = box_x;
            int btn2_x = box_x + box_w - btn_w;
            
            if (y >= btn_y && y <= btn_y + btn_h) {
                if (x >= btn1_x && x <= btn1_x + btn_w) {
                    if (!ui.jumpInputText.empty()) {
                        int page = std::stoi(ui.jumpInputText);
                        ui.jumpToPage(page - 1);
                        ui.jumpInputText = "";
                        s_current_state = State::READER;
                        ui.drawReadingView();
                    }
                } else if (x >= btn2_x && x <= btn2_x + btn_w) {
                    if (!ui.jumpInputText.empty()) {
                        float percent = std::stof(ui.jumpInputText);
                        ui.jumpToPercentage(percent);
                        ui.jumpInputText = "";
                        s_current_state = State::READER;
                        ui.drawReadingView();
                    }
                }
            } else {
                ui.jumpInputText = "";
                s_current_state = State::READER;
                ui.drawReadingView();
            }
        }
    }
}

void state_machine_task(void *pvParameters) {
    SystemMessage_t msg;
    while (1) {
        if (xQueueReceive(g_state_machine_queue, &msg, portMAX_DELAY) == pdTRUE) {
            Page* currentPage = ui.getCurrentPage();
            if (currentPage) {
                State nextState = currentPage->handleEvent(msg);
                if (nextState != s_current_state) {
                    ui.setPageByState(nextState);
                    s_current_state = nextState;
                    ui.drawCurrentPage();
                }
            } else {
                // Fallback for pages not yet refactored
                switch (s_current_state) {
                    case State::READER:
                        handle_state_reader(msg);
                        break;
                    case State::READER_MENU:
                        handle_state_reader_menu(msg);
                        break;
                    case State::READER_JUMP_MENU:
                        handle_state_reader_jump_menu(msg);
                        break;
                    case State::CONTINUE:
                    case State::TRANSFER:
                        handle_state_generic_page(msg);
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

bool send_message(const SystemMessage_t& msg) {
    if (g_state_machine_queue == NULL) return false;
    return xQueueSend(g_state_machine_queue, &msg, 0) == pdTRUE;
}
