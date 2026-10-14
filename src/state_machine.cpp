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
        // FreeType glyph loading (TTF fonts) needs more than the old 4 KB.
        8192,
        NULL,
        1,
        NULL
    );
}

void set_current_state(State state) {
    s_current_state = state;
}

State get_current_state() {
    return s_current_state;
}

void state_machine_task(void *pvParameters) {
    // Idle wait is bounded so work started outside this task (boot restore
    // opens the book from setup()) is picked up without an input event.
    const TickType_t kIdleWaitTicks = pdMS_TO_TICKS(200);
    SystemMessage_t msg;
    while (1) {
        // Background page indexing runs here, between input events, so it
        // never overlaps a page turn (they share the SD card and glyph cache).
        bool background = ui.hasBackgroundWork();
        if (xQueueReceive(g_state_machine_queue, &msg,
                          background ? 0 : kIdleWaitTicks) != pdTRUE) {
            if (background) {
                ui.backgroundStep();
                vTaskDelay(1);  // Let the idle task and loopTask run.
            }
            continue;
        }
        Page* currentPage = ui.getCurrentPage();
        if (!currentPage) continue;
        ui.mapInputToPage(&msg);
        State nextState = currentPage->handleEvent(msg);
        if (nextState != s_current_state) {
            // Pages read get_current_state() in enter() to see where they
            // were opened from, so the state changes after setPageByState().
            ui.setPageByState(nextState);
            s_current_state = nextState;
            ui.drawCurrentPage();
        }
    }
}

bool send_message(const SystemMessage_t& msg) {
    if (g_state_machine_queue == NULL) return false;
    return xQueueSend(g_state_machine_queue, &msg, 0) == pdTRUE;
}
