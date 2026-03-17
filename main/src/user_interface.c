#include "esp_log.h"

#include "../include/user_interface.h"
#include "../include/ui_internal.h"
#include "../include/ui_screens.h"
#include "../include/ui_widgets.h"

#include "../include/trace_system.h"
#include "../include/table_fsm.h"
#include "../include/trace_scheduler.h"
#include "../include/types.h"
#include "../include/display_util.h"
#include "../include/touch_controller_util.h"
#include "../include/font5x7.h"
#include "../include/haptic_driver.h"
#include "../include/battery_monitor.h"

#include "driver/spi_master.h"
#include <string.h>
/* ------------------------------------------------------ */


typedef enum { 
    UI_MODE_MAIN, 
    UI_MODE_TABLE_GRID,
    UI_MODE_TABLE_INFO,
} ui_mode;


static const char *TAG_UI = "ui";

static volatile ui_mode UI_MODE = UI_MODE_TABLE_GRID;
static volatile ui_snapshot UI_SNAPSHOT;

static bool last_touch_pressed = false;

static const task_id UNINITIALISED_TASK_ID = { UINT16_MAX, UINT16_MAX };
static ui_task_state UI_TASK_STATE = UI_TASK_STATE_IDLE;
static task_id UI_LOCKED_TASK_ID = { UINT16_MAX, 0 };


/* --------------------------- Internal functions --------------------------- */

static inline bool point_in_rect(uint16_t x, uint16_t y, rect region)
{
    const int x_padding = 10;
    const int y_padding = 5;

    int x_left = (int)region.x - x_padding;
    if (x_left < 0) x_left = 0;
    int x_right = (int)region.x + region.w + x_padding;
    if (x_right >= DISPLAY_WIDTH) x_right = DISPLAY_WIDTH;
    int y_top = (int)region.y - y_padding;
    if (y_top < 0) y_top = 0;
    int y_bottom = (int)region.y + region.h + y_padding;
    if (y_bottom >= DISPLAY_HEIGHT) y_bottom = DISPLAY_HEIGHT;

    return (x >= x_left) && (x < x_right) &&
           (y >= y_top) && (y < y_bottom);
}


static void ui_update_snapshot_from_system() {
    const task *t = system_get_active_task();
    if (!t) {
        UI_SNAPSHOT.has_task = false;
        UI_SNAPSHOT.task_id = INVALID_TASK_ID;
        UI_SNAPSHOT.table_number = 0;
        UI_SNAPSHOT.task_kind = 0;
        return;
    }

    UI_SNAPSHOT.has_task = true;
    UI_SNAPSHOT.task_id = t->id;
    UI_SNAPSHOT.table_number = t->table_number;
    UI_SNAPSHOT.task_kind = t->kind;
}


/* Execute a button action (called on touch-up after press animation). */
static void execute_button_action(spi_device_handle_t display, ui_action act,
                                  ui_snapshot snap, time_ms now, uint8_t sel_table) {
    switch (act) {
        case UI_ACTION_IGNORE:
            if (snap.has_task)
                system_apply_user_action_to_task(snap.task_id, USER_ACTION_IGNORE, now);
            break;
        case UI_ACTION_BILL:
            system_apply_table_fsm_event(snap.table_number, EVENT_TABLE_REQUESTED_BILL, now);
            break;
        case UI_ACTION_START_TASK:
            if (snap.has_task) {
                UI_TASK_STATE = UI_TASK_STATE_IN_PROGRESS;
                UI_LOCKED_TASK_ID = snap.task_id;
                draw_button_complete(display);
            }
            break;
        case UI_ACTION_COMPLETE:
            if (UI_TASK_STATE == UI_TASK_STATE_IN_PROGRESS) {
                system_apply_user_action_to_task(UI_LOCKED_TASK_ID, USER_ACTION_COMPLETE, now);
                UI_TASK_STATE = UI_TASK_STATE_IDLE;
                UI_LOCKED_TASK_ID = INVALID_TASK_ID;
                draw_button_start(display);
            }
            break;
        case UI_ACTION_TAKE_ORDER:
            system_apply_table_fsm_event(snap.table_number, EVENT_TAKE_ORDER_EARLY_OR_REPEAT, now);
            break;
        case UI_ACTION_TABLE_INFO_TAKE_ORDER:
            if (state_can_take_order(system_get_table_state(sel_table))) {
                system_apply_table_fsm_event(sel_table, EVENT_TAKE_ORDER_EARLY_OR_REPEAT, now);
                UI_MODE = UI_MODE_MAIN;
                ui_draw_main(display, snap, UI_TASK_STATE);
            }
            break;
        case UI_ACTION_TABLE_INFO_BILL:
            if (state_can_request_bill(system_get_table_state(sel_table))) {
                system_apply_table_fsm_event(sel_table, EVENT_TABLE_REQUESTED_BILL, now);
                UI_MODE = UI_MODE_MAIN;
                ui_draw_main(display, snap, UI_TASK_STATE);
            }
            break;
        case UI_ACTION_TABLE_INFO_UNDO:
            if (table_can_undo(system_get_table(sel_table))) {
                system_apply_table_fsm_event(sel_table, EVENT_UNDO, now);
                draw_active_table_page(display, sel_table);
            }
            break;
        default: break;
    }
}


static inline bool task_id_equal(task_id a, task_id b) {
    return a.index == b.index && a.generation == b.generation;
}


/* ------------ Button callbacks ------------ */
static ui_action decode_touch_main(uint16_t x, uint16_t y, ui_snapshot snap) {
    task_kind kind = snap.task_kind;
    if (point_in_rect(x, y, MAIN_IGNORE_BTN) && snap.has_task && kind != MONITOR_TABLE) {
        return UI_ACTION_IGNORE;
    }

    if (point_in_rect(x, y, MAIN_BILL_BTN) && kind == MONITOR_TABLE) {
        return UI_ACTION_BILL;
    }

    if (point_in_rect(x, y, MAIN_START_BTN) && UI_TASK_STATE == UI_TASK_STATE_READY) {
        return UI_ACTION_START_TASK;
    }

    if (point_in_rect(x, y, MAIN_COMPLETE_BTN) && UI_TASK_STATE == UI_TASK_STATE_IN_PROGRESS) {
        return UI_ACTION_COMPLETE;
    }

    if (point_in_rect(x, y, MAIN_TAKEORDER_BTN)) {
        return UI_ACTION_TAKE_ORDER;
    }

    if (point_in_rect(x, y, MAIN_TABLES_BTN)) {
        return UI_ACTION_OPEN_TABLES;
    }

    return UI_ACTION_NONE;
}


static ui_action decode_touch_grid(uint16_t x, uint16_t y) {
    if (point_in_rect(x, y, TOPBAR_BACK_BTN))     return UI_ACTION_BACK;
    if (point_in_rect(x, y, TABLE_GRID_PREV_BTN)) return UI_ACTION_GRID_PREV_PAGE;
    if (point_in_rect(x, y, TABLE_GRID_NEXT_BTN)) return UI_ACTION_GRID_NEXT_PAGE;

    for (uint8_t slot = 0; slot < TABLES_PER_PAGE; slot++) {
        rect tile = table_tile_rect(slot);
        if (point_in_rect(x, y, tile)) {
            return (ui_action)(UI_ACTION_TABLE_TILE_1 + slot);
        }
    }
    return UI_ACTION_NONE;
}


static ui_action decode_touch_table_info(uint16_t x, uint16_t y) {
    if (point_in_rect(x, y, TOPBAR_BACK_BTN))           return UI_ACTION_TABLE_INFO_BACK;
    if (point_in_rect(x, y, TABLE_INFO_TAKE_ORDER_BTN)) return UI_ACTION_TABLE_INFO_TAKE_ORDER;
    if (point_in_rect(x, y, TABLE_INFO_BILL_BTN))       return UI_ACTION_TABLE_INFO_BILL;
    if (point_in_rect(x, y, TABLE_INFO_UNDO_BTN))       return UI_ACTION_TABLE_INFO_UNDO;

    return UI_ACTION_NONE;
}


void ui_task(void *arg) {
    display_spi_ctx display = *(display_spi_ctx *)arg;
    task_id prev_task_id = UNINITIALISED_TASK_ID;
    static uint8_t SELECTED_TABLE = 0xFF;
    uint8_t prev_bars = 0xFF;
    uint32_t batt_tick = 0;

    bool display_sleeping = false;
    time_ms last_activity_ms = get_time();
    ui_action PENDING_ACTION = UI_ACTION_NONE;

    // Top-right corner hold state for sleep gesture
    bool corner_hold_active = false;
    time_ms corner_hold_start_ms = 0;
    bool sleep_triggered_this_hold = false;

    while (1) {
        time_ms now = get_time();
        uint16_t x = 0, y = 0;
        bool pressed = read_touch_point(&x, &y);

        // --- Wake from sleep on any touch edge ---
        if (display_sleeping) {
            if (pressed && !last_touch_pressed) {
                display_sleeping = false;
                display_backlight_set(true);
                last_activity_ms = now;
                corner_hold_active = false;
                sleep_triggered_this_hold = false;
            }
            last_touch_pressed = pressed;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // --- Inactivity sleep (30 s with no touch) ---
        if ((now - last_activity_ms) >= UI_INACTIVITY_SLEEP_MS) {
            display_sleeping = true;
            display_backlight_set(false);
            corner_hold_active = false;
            sleep_triggered_this_hold = false;
            last_touch_pressed = pressed;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // --- Top-right corner hold-to-sleep gesture ---
        bool in_sleep_corner = pressed &&
                               (x >= UI_SLEEP_CORNER_MIN_X) &&
                               (y <= UI_SLEEP_CORNER_MAX_Y);

        if (in_sleep_corner && !corner_hold_active) {
            corner_hold_active = true;
            corner_hold_start_ms = now;
            sleep_triggered_this_hold = false;
        } else if (!pressed) {
            corner_hold_active = false;
            sleep_triggered_this_hold = false;
        }

        if (corner_hold_active && !sleep_triggered_this_hold) {
            if ((now - corner_hold_start_ms) >= UI_SLEEP_HOLD_MS) {
                sleep_triggered_this_hold = true;
                display_sleeping = true;
                display_backlight_set(false);
                last_touch_pressed = pressed;
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
        }

        if (pressed) {
            last_activity_ms = now;
        }

        // --- Normal UI update ---
        ui_update_snapshot_from_system();
        // Copy the snapshot once per press to avoid mixed fields
        ui_snapshot snap = UI_SNAPSHOT;

        if (UI_MODE == UI_MODE_MAIN && UI_TASK_STATE != UI_TASK_STATE_IN_PROGRESS) {
            if (!task_id_equal(snap.task_id, prev_task_id)) {
                if (snap.has_task) {
                    if (UI_TASK_STATE == UI_TASK_STATE_READY || UI_TASK_STATE == UI_TASK_STATE_IDLE) {
                        drv2605l_play_effect(1);
                    }
                    UI_TASK_STATE = UI_TASK_STATE_READY;
                    UI_LOCKED_TASK_ID = snap.task_id;
                } else {
                    UI_TASK_STATE = UI_TASK_STATE_IDLE;
                    UI_LOCKED_TASK_ID = INVALID_TASK_ID;
                }

                ui_draw_main(display.dev_handle, snap, UI_TASK_STATE);
                prev_task_id = snap.task_id;
            }
        }

        if (pressed && !last_touch_pressed) {
            ui_action act = UI_ACTION_NONE;
            switch (UI_MODE) {
                case UI_MODE_MAIN: {
                    act = decode_touch_main(x, y, snap);
                    if (act == UI_ACTION_OPEN_TABLES) {
                        UI_GRID_PAGE = 0;
                        UI_MODE = UI_MODE_TABLE_GRID;
                        ui_draw_grid(display.dev_handle);
                    } else if (act != UI_ACTION_NONE) {
                        draw_button_highlight(display.dev_handle, act);
                        PENDING_ACTION = act;
                    }
                } break;

                case UI_MODE_TABLE_GRID: {
                    act = decode_touch_grid(x, y);
                    const uint8_t num_pages = (NUM_OF_TABLES + TABLES_PER_PAGE - 1) / TABLES_PER_PAGE;

                    if (act == UI_ACTION_BACK) {
                        UI_MODE = UI_MODE_MAIN;
                        ui_draw_main(display.dev_handle, snap, UI_TASK_STATE);
                        break;
                    }

                    if (act == UI_ACTION_GRID_PREV_PAGE && UI_GRID_PAGE > 0) {
                        UI_GRID_PAGE--;
                        ui_draw_grid(display.dev_handle);
                        break;
                    }

                    if (act == UI_ACTION_GRID_NEXT_PAGE && UI_GRID_PAGE < num_pages - 1) {
                        UI_GRID_PAGE++;
                        ui_draw_grid(display.dev_handle);
                        break;
                    }

                    if (act >= UI_ACTION_TABLE_TILE_1 && act <= UI_ACTION_TABLE_TILE_9) {
                        uint8_t slot = (uint8_t)(act - UI_ACTION_TABLE_TILE_1);
                        uint8_t table = UI_GRID_PAGE * TABLES_PER_PAGE + slot;

                        if (table >= NUM_OF_TABLES) break;

                        table_state tapped_table_state = system_get_table_state(table);
                        if (tapped_table_state == TABLE_IDLE) {
                            system_apply_table_fsm_event(table, EVENT_CUSTOMERS_SEATED, now);
                            UI_MODE = UI_MODE_MAIN;
                            ui_draw_main(display.dev_handle, snap, UI_TASK_STATE);
                        } else {
                            SELECTED_TABLE = table;
                            UI_MODE = UI_MODE_TABLE_INFO;
                            draw_active_table_page(display.dev_handle, table);
                        }
                    }
                } break;

                case UI_MODE_TABLE_INFO: {
                    act = decode_touch_table_info(x, y);
                    if (act == UI_ACTION_TABLE_INFO_BACK) {
                        UI_MODE = UI_MODE_TABLE_GRID;
                        ui_draw_grid(display.dev_handle);
                    } else if (act != UI_ACTION_NONE) {
                        draw_button_highlight(display.dev_handle, act);
                        PENDING_ACTION = act;
                    }
                } break;

                default:
                    break;
            }

            ESP_LOGI(TAG_UI, "touch down x=%u y=%u mode=%d act=%d",
                     (unsigned)x, (unsigned)y, (int)UI_MODE, (int)act);
        }

        // Touch UP: restore button appearance then execute the pending action
        if (!pressed && last_touch_pressed && PENDING_ACTION != UI_ACTION_NONE) {
            ui_action pact = PENDING_ACTION;
            PENDING_ACTION = UI_ACTION_NONE;
            restore_button(display.dev_handle, pact, SELECTED_TABLE);
            ui_update_snapshot_from_system();
            snap = UI_SNAPSHOT;
            execute_button_action(display.dev_handle, pact, snap, now, SELECTED_TABLE);
        }

        last_touch_pressed = pressed;

        // Update battery reading every ~10 s (200 × 50 ms ticks)
        if (++batt_tick >= 200) {
            batt_tick = 0;
            battery_monitor_update();
        }

        uint8_t current_bars = battery_monitor_get_bars();
        if (current_bars != prev_bars) {
            draw_battery_icon(display.dev_handle, current_bars);
            prev_bars = current_bars;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
