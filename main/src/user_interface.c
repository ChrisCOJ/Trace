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
    UI_MODE_CONFIRM_SWITCH,
} ui_mode;


typedef enum {
    UI_URGENCY_ON_TIME = 0,
    UI_URGENCY_OVERDUE = 1,
    UI_URGENCY_CRITICAL = 2,
} ui_urgency_band;


static const char *TAG_UI = "ui";

static volatile ui_mode UI_MODE = UI_MODE_TABLE_GRID;
static volatile ui_snapshot UI_SNAPSHOT;

static bool last_touch_pressed = false;

static const task_id UNINITIALISED_TASK_ID = { UINT16_MAX, UINT16_MAX };

static bool undo_available = false;
static uint8_t undo_table = 0;
static time_ms undo_start_ms = 0;

static bool undo_ignore_available = false;
static task_id undo_ignore_task_id;
static time_ms undo_ignore_start_ms = 0;
static uint8_t undo_ignore_prev_count = 0;
static time_ms undo_ignore_prev_suppress = 0;

static uint8_t SELECTED_TABLE = 0xFF;

static task_id switch_denied_task_id        = { UINT16_MAX, UINT16_MAX };
static task_id active_task_id_during_switch = { UINT16_MAX, UINT16_MAX };
static task_id switch_overlay_task_id       = { UINT16_MAX, UINT16_MAX };

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


static inline bool task_id_equal(task_id a, task_id b) {
    return a.index == b.index && a.generation == b.generation;
}


static void force_current_table_task_to_main(uint8_t table_number, time_ms now)
{
    task *t = system_get_current_task_pointer_for_table(table_number);
    if (!t) return;

    system_force_active_task(t->id, now);
}


static void ui_update_snapshot_from_system() {
    const task *task_inst = system_get_active_task();
    if (!task_inst) {
        UI_SNAPSHOT.has_task     = false;
        UI_SNAPSHOT.task_id      = INVALID_TASK_ID;
        UI_SNAPSHOT.table_number = 0;
        UI_SNAPSHOT.task_kind    = TASK_NOT_APPLICABLE;
        UI_SNAPSHOT.urgency_level = UI_URGENCY_ON_TIME;
        UI_SNAPSHOT.deadline      = 0;
    } else {
        time_ms current_time_ms = get_time();
        uint8_t urgency_level = UI_URGENCY_ON_TIME;
        if (current_time_ms > task_inst->time_limit)
            urgency_level = ((current_time_ms - task_inst->time_limit) >= TASK_CRITICAL_OVRDUE_TIME_LIMIT[(int)task_inst->kind]) 
                ? UI_URGENCY_CRITICAL : UI_URGENCY_OVERDUE;

        UI_SNAPSHOT.has_task      = true;
        UI_SNAPSHOT.task_id       = task_inst->id;
        UI_SNAPSHOT.table_number  = task_inst->table_number;
        UI_SNAPSHOT.task_kind     = task_inst->kind;
        UI_SNAPSHOT.urgency_level = urgency_level;
        UI_SNAPSHOT.deadline      = task_inst->time_limit;
    }

    UI_SNAPSHOT.pending_count  = system_get_pending_count();
    UI_SNAPSHOT.critical_count = system_get_critical_pending_count();

    const task *top_critical_task = system_get_top_critical_task();
    if (top_critical_task) {
        UI_SNAPSHOT.critical_task_id      = top_critical_task->id;
        UI_SNAPSHOT.critical_task_kind    = top_critical_task->kind;
        UI_SNAPSHOT.critical_table_number = top_critical_task->table_number;
        UI_SNAPSHOT.critical_deadline     = top_critical_task->time_limit;
    } else {
        UI_SNAPSHOT.critical_task_id      = INVALID_TASK_ID;
        UI_SNAPSHOT.critical_task_kind    = TASK_NOT_APPLICABLE;
        UI_SNAPSHOT.critical_table_number = 0xFF;
        UI_SNAPSHOT.critical_deadline     = 0;
    }
}


/* ---------------- Page mode wrappers ---------------- */
static void ui_enter_main(spi_device_handle_t display, task_id *prev_task_id) {
    UI_MODE = UI_MODE_MAIN;
    ui_update_snapshot_from_system();
    ui_draw_main(display, UI_SNAPSHOT);

    if (undo_available || undo_ignore_available) {
        draw_button(display, MAIN_IGNORE_BTN, "Undo", BTN_SECONDARY);
    }

    *prev_task_id = UI_SNAPSHOT.task_id;
}


static void ui_enter_grid(spi_device_handle_t display) {
    UI_MODE = UI_MODE_TABLE_GRID;
    ui_draw_grid(display);
}


static void ui_enter_table_info(spi_device_handle_t display, uint8_t table_number) {
    SELECTED_TABLE = table_number;
    UI_MODE = UI_MODE_TABLE_INFO;
    draw_active_table_page(display, table_number);
}


static void ui_enter_switch_prompt(spi_device_handle_t display, ui_snapshot snap) {
    UI_MODE = UI_MODE_CONFIRM_SWITCH;
    switch_overlay_task_id = snap.critical_task_id;
    ui_draw_switch_prompt(display, snap);
}


/* Execute a button action (called on touch-up after press animation). */
static void execute_button_action(spi_device_handle_t display, ui_action act,
                                  ui_snapshot snap, time_ms now, uint8_t sel_table,
                                  task_id *prev_task_id) {
    switch (act) {
        case UI_ACTION_IGNORE:
            if (snap.has_task) {
                const task *t = system_get_active_task();
                undo_ignore_prev_count    = t ? t->ignore_count    : 0;
                undo_ignore_prev_suppress = t ? t->suppress_until  : 0;
                undo_ignore_task_id       = snap.task_id;
                system_apply_user_action_to_task(snap.task_id, USER_ACTION_IGNORE, now);
                undo_ignore_available = true;
                undo_available        = false;
                undo_ignore_start_ms  = now;
                draw_button(display, MAIN_IGNORE_BTN, "Undo", BTN_SECONDARY);
            }
            break;
        case UI_ACTION_BILL:
            system_apply_table_fsm_event(snap.table_number, EVENT_TABLE_REQUESTED_BILL, now);
            force_current_table_task_to_main(snap.table_number, now);
            ui_enter_main(display, prev_task_id);
            break;
        case UI_ACTION_COMPLETE: {
            if (snap.has_task) {
                system_apply_user_action_to_task(snap.task_id, USER_ACTION_COMPLETE, now);
                undo_available        = true;
                undo_ignore_available = false;
                undo_table            = snap.table_number;
                undo_start_ms         = now;
                draw_button(display, MAIN_IGNORE_BTN, "Undo", BTN_SECONDARY);
            }
            break;
        }
        case UI_ACTION_MAIN_UNDO:
            if (undo_available) {
                system_apply_table_fsm_event(undo_table, EVENT_UNDO, now);
                undo_available = false;
                draw_button(display, MAIN_IGNORE_BTN, "Ignore", BTN_DISABLED);
            } else if (undo_ignore_available) {
                system_undo_task_ignore(undo_ignore_task_id, undo_ignore_prev_count, undo_ignore_prev_suppress, now);
                undo_ignore_available = false;
                ui_update_snapshot_from_system();
                draw_button(display, MAIN_IGNORE_BTN, "Ignore",
                            UI_SNAPSHOT.has_task ? BTN_WARNING : BTN_DISABLED);
            }
            break;
        case UI_ACTION_TAKE_ORDER:
            system_apply_table_fsm_event(snap.table_number, EVENT_TAKE_ORDER_EARLY_OR_REPEAT, now);
            force_current_table_task_to_main(snap.table_number, now);
            ui_enter_main(display, prev_task_id);
            break;
        case UI_ACTION_TABLE_INFO_TAKE_ORDER:
            if (state_can_take_order(system_get_table_state(sel_table))) {
                system_apply_table_fsm_event(sel_table, EVENT_TAKE_ORDER_EARLY_OR_REPEAT, now);
                force_current_table_task_to_main(sel_table, now);
                ui_enter_main(display, prev_task_id);
            }
            break;
        case UI_ACTION_TABLE_INFO_BILL:
            if (state_can_request_bill(system_get_table_state(sel_table))) {
                system_apply_table_fsm_event(sel_table, EVENT_TABLE_REQUESTED_BILL, now);
                force_current_table_task_to_main(sel_table, now);
                ui_enter_main(display, prev_task_id);
            }
            break;
        case UI_ACTION_TABLE_INFO_UNDO:
            if (table_can_undo(system_get_table(sel_table))) {
                system_apply_table_fsm_event(sel_table, EVENT_UNDO, now);
                ui_enter_table_info(display, sel_table);
            }
            break;
        default: break;
    }
}


/* ------------ Button callbacks ------------ */
static ui_action decode_touch_main(uint16_t x, uint16_t y, ui_snapshot snap) {
    task_kind kind = snap.task_kind;

    if (point_in_rect(x, y, MAIN_TABLES_BTN)) {
        return UI_ACTION_OPEN_TABLES;
    }

    if (point_in_rect(x, y, MAIN_COMPLETE_BTN) &&
        snap.has_task) {
        return UI_ACTION_COMPLETE;
    }

    // Bottom button area: undo (post-completion or post-ignore) takes priority over ignore
    if (point_in_rect(x, y, MAIN_IGNORE_BTN)) {
        if (undo_available || undo_ignore_available) return UI_ACTION_MAIN_UNDO;
        if (snap.has_task && kind != MONITOR_TABLE) return UI_ACTION_IGNORE;
    }
    if (point_in_rect(x, y, MAIN_BILL_BTN) && kind == MONITOR_TABLE) {
        return UI_ACTION_BILL;
    }
    if (point_in_rect(x, y, MAIN_TAKEORDER_BTN) && kind == MONITOR_TABLE) {
        return UI_ACTION_TAKE_ORDER;
    }

    // Task info area tap → jump directly to the active task's table info
    if (point_in_rect(x, y, MAIN_TASK_AREA) && snap.has_task) {
        return UI_ACTION_OPEN_TASK_TABLE;
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


static ui_action decode_touch_confirm(uint16_t x, uint16_t y) {
    if (point_in_rect(x, y, CONFIRM_ALLOW_BTN)) return UI_ACTION_CONFIRM_ALLOW;
    if (point_in_rect(x, y, CONFIRM_DENY_BTN))  return UI_ACTION_CONFIRM_DENY;
    return UI_ACTION_NONE;
}


static ui_action decode_touch_table_info(uint16_t x, uint16_t y) {
    if (point_in_rect(x, y, TOPBAR_BACK_BTN))           return UI_ACTION_TABLE_INFO_BACK;
    if (point_in_rect(x, y, TABLE_INFO_TAKE_ORDER_BTN)) return UI_ACTION_TABLE_INFO_TAKE_ORDER;
    if (point_in_rect(x, y, TABLE_INFO_BILL_BTN))       return UI_ACTION_TABLE_INFO_BILL;
    if (point_in_rect(x, y, TABLE_INFO_UNDO_BTN))       return UI_ACTION_TABLE_INFO_UNDO;

    return UI_ACTION_NONE;
}


static void expire_undo_buttons(spi_device_handle_t display, time_ms now, ui_snapshot snap) {
    if (undo_available && (now - undo_start_ms) >= UI_UNDO_TIMEOUT_MS) {
        undo_available = false;
        if (UI_MODE == UI_MODE_MAIN)
            draw_button(display, MAIN_IGNORE_BTN, "Ignore", snap.has_task ? BTN_WARNING : BTN_DISABLED);
    }
    if (undo_ignore_available && (now - undo_ignore_start_ms) >= UI_UNDO_TIMEOUT_MS) {
        undo_ignore_available = false;
        if (UI_MODE == UI_MODE_MAIN)
            draw_button(display, MAIN_IGNORE_BTN, "Ignore", snap.has_task ? BTN_WARNING : BTN_DISABLED);
    }
}



static void sync_main_task_state(spi_device_handle_t display, ui_snapshot snap, task_id *prev_task_id) {
    if (UI_MODE != UI_MODE_MAIN) return;

    if (!task_id_equal(snap.task_id, *prev_task_id)) {
        ui_enter_main(display, prev_task_id);
    }
}


static void process_touch_down(spi_device_handle_t display, uint16_t x, uint16_t y,
                               ui_snapshot snap, ui_action *pending_action) {
    ui_action act = UI_ACTION_NONE;
    switch (UI_MODE) {
        case UI_MODE_MAIN:
            act = decode_touch_main(x, y, snap);
            if (act != UI_ACTION_NONE) {
                draw_button_highlight(display, act);
                *pending_action = act;
            }
            break;
        case UI_MODE_TABLE_GRID:
            act = decode_touch_grid(x, y);
            if (act != UI_ACTION_NONE)
                *pending_action = act;
            break;
        case UI_MODE_TABLE_INFO:
            act = decode_touch_table_info(x, y);
            if (act != UI_ACTION_NONE) {
                draw_button_highlight(display, act);
                *pending_action = act;
            }
            break;
        case UI_MODE_CONFIRM_SWITCH:
            act = decode_touch_confirm(x, y);
            if (act != UI_ACTION_NONE) {
                draw_button_highlight(display, act);
                *pending_action = act;
            }
            break;
        default:
            break;
    }
    ESP_LOGI(TAG_UI, "touch down x=%u y=%u mode=%d act=%d",
             (unsigned)x, (unsigned)y, (int)UI_MODE, (int)act);
}


static void handle_swipe(spi_device_handle_t display, int16_t dx, ui_action *pending_action, task_id *prev_task_id) {
    *pending_action = UI_ACTION_NONE;
    const uint8_t num_pages = (NUM_OF_TABLES + TABLES_PER_PAGE - 1) / TABLES_PER_PAGE;
    if (UI_MODE == UI_MODE_MAIN) {
        UI_GRID_PAGE = 0;
        ui_enter_grid(display);
    } else if (dx > (int16_t)UI_SWIPE_THRESHOLD) {
        // Swipe right: go to previous page, or back to main from page 0
        if (UI_GRID_PAGE > 0) {
            UI_GRID_PAGE--;
            ui_enter_grid(display);
        } else {
            ui_enter_main(display, prev_task_id);
        }
    } else {
        // Swipe left: go to next page
        if (UI_GRID_PAGE < num_pages - 1) {
            UI_GRID_PAGE++;
            ui_enter_grid(display);
        }
    }
}


static void dispatch_action(spi_device_handle_t display, ui_action pact, time_ms now, task_id *prev_task_id) {
    restore_button(display, pact, SELECTED_TABLE);
    ui_update_snapshot_from_system();
    ui_snapshot snap = UI_SNAPSHOT;

    const uint8_t num_pages = (NUM_OF_TABLES + TABLES_PER_PAGE - 1) / TABLES_PER_PAGE;
    switch (pact) {
        case UI_ACTION_OPEN_TABLES:
            UI_GRID_PAGE = 0;
            ui_enter_grid(display);
            break;
        case UI_ACTION_OPEN_TASK_TABLE:
            ui_enter_table_info(display, snap.table_number);
            break;
        case UI_ACTION_BACK:
            ui_enter_main(display, prev_task_id);
            break;
        case UI_ACTION_GRID_PREV_PAGE:
            if (UI_GRID_PAGE > 0) { 
                UI_GRID_PAGE--; 
                ui_enter_grid(display); 
            }
            break;
        case UI_ACTION_GRID_NEXT_PAGE:
            if (UI_GRID_PAGE < num_pages - 1) { 
                UI_GRID_PAGE++; 
                ui_enter_grid(display);
            }
            break;
        case UI_ACTION_TABLE_INFO_BACK:
            ui_enter_main(display, prev_task_id);
            break;
        case UI_ACTION_CONFIRM_ALLOW:
            system_force_active_task(switch_overlay_task_id, now);
            switch_denied_task_id        = UNINITIALISED_TASK_ID;
            active_task_id_during_switch = UNINITIALISED_TASK_ID;
            switch_overlay_task_id       = UNINITIALISED_TASK_ID;
            ui_enter_main(display, prev_task_id);
            break;
        case UI_ACTION_CONFIRM_DENY:
            switch_denied_task_id        = switch_overlay_task_id;
            active_task_id_during_switch = snap.task_id;
            switch_overlay_task_id       = UNINITIALISED_TASK_ID;
            ui_enter_main(display, prev_task_id);
            break;
        default:
            if (pact >= UI_ACTION_TABLE_TILE_1 && pact <= UI_ACTION_TABLE_TILE_9) {
                uint8_t slot = (uint8_t)(pact - UI_ACTION_TABLE_TILE_1);
                uint8_t table = UI_GRID_PAGE * TABLES_PER_PAGE + slot;
                if (table < NUM_OF_TABLES) {
                    if (system_get_table_state(table) == TABLE_IDLE) {
                        system_apply_table_fsm_event(table, EVENT_CUSTOMERS_SEATED, now);
                        ui_enter_main(display, prev_task_id);
                    } else {
                        ui_enter_table_info(display, table);
                    }
                }
            } else {
                execute_button_action(display, pact, snap, now, SELECTED_TABLE, prev_task_id);
            }
            break;
    }
}


static void tick_periodic_updates(spi_device_handle_t display, bool display_sleeping) {
    static uint32_t time_tick = 0;
    static uint32_t batt_tick = 0;
    static uint8_t prev_bars          = 0xFF;
    static uint8_t prev_pending_count  = 0xFF;
    static uint8_t prev_critical_count = 0xFF;

    if (++time_tick >= 20) {
        time_tick = 0;
        if (UI_MODE == UI_MODE_MAIN && !display_sleeping) {
            ui_update_snapshot_from_system();
            ui_draw_main_time(display, UI_SNAPSHOT);
        }
    }

    if (++batt_tick >= 200) {
        batt_tick = 0;
        battery_monitor_update();
    }

    uint8_t current_bars = battery_monitor_get_bars();
    if (current_bars != prev_bars) {
        draw_battery_icon(display, current_bars);
        prev_bars = current_bars;
    }

    if (UI_MODE == UI_MODE_MAIN && !display_sleeping) {
        uint8_t p = UI_SNAPSHOT.pending_count;
        uint8_t c = UI_SNAPSHOT.critical_count;
        if (p != prev_pending_count || c != prev_critical_count) {
            draw_pending_badge(display, p, c);
            prev_pending_count  = p;
            prev_critical_count = c;
        }
    }
}


void ui_task(void *arg) {
    display_spi_ctx display = *(display_spi_ctx *)arg;
    task_id prev_task_id = UNINITIALISED_TASK_ID;

    bool display_sleeping = false;
    time_ms last_activity_ms = get_time();
    ui_action PENDING_ACTION = UI_ACTION_NONE;

    // Top-right corner hold state for sleep gesture
    bool corner_hold_active = false;
    time_ms corner_hold_start_ms = 0;
    bool sleep_triggered_this_hold = false;

    // Swipe tracking for main - grid navigation
    uint16_t swipe_start_x = 0;
    uint16_t last_pressed_x = 0;

    // Haptic + wake state for task-change, urgency-1, and urgency-2 notifications
    bool urgent_notified = false;
    bool urgent_level1_notified = false;
    task_id last_urgent_task_id   = UNINITIALISED_TASK_ID;
    task_id last_critical_task_id = UNINITIALISED_TASK_ID;

    while (1) {
        time_ms now = get_time();
        uint16_t x = 0, y = 0;
        bool pressed = read_touch_point(&x, &y);

        // Haptic notifications (run regardless of sleep state)
        // Any haptic trigger also wakes the display.
        {
#define WAKE_IF_SLEEPING() do { \
    if (display_sleeping) { \
        display_sleeping = false; \
        display_backlight_set(true); \
        last_activity_ms = now; \
    } \
} while (0)

            const task *active = system_get_active_task();
            if (active) {
                if (!task_id_equal(active->id, last_urgent_task_id)) {
                    drv2605l_play_effect(1);
                    WAKE_IF_SLEEPING();
                    urgent_notified = false;
                    urgent_level1_notified = false;
                    last_urgent_task_id = active->id;
                }
                bool is_overdue = (now > active->time_limit);
                bool is_critically_overdue = is_overdue &&
                                             (now - active->time_limit) >= TASK_CRITICAL_OVRDUE_TIME_LIMIT[(int)active->kind];
                // Urgency level 1: task just became overdue — single click + wake
                if (is_overdue && !urgent_level1_notified) {
                    drv2605l_play_effect(1);
                    urgent_level1_notified = true;
                    WAKE_IF_SLEEPING();
                }
                // Urgency level 2: critically overdue (≥5 min) — triple click + wake
                if (is_critically_overdue && !urgent_notified) {
                    drv2605l_play_urgent_pattern();
                    urgent_notified = true;
                    WAKE_IF_SLEEPING();
                }
            } else {
                urgent_notified        = false;
                urgent_level1_notified = false;
                last_urgent_task_id    = UNINITIALISED_TASK_ID;
            }

            // Critical pending task — urgent haptic + wake on first detection
            const task *critical_pending = system_get_top_critical_task();
            if (critical_pending) {
                if (!task_id_equal(critical_pending->id, last_critical_task_id)) {
                    drv2605l_play_urgent_pattern();
                    WAKE_IF_SLEEPING();
                    last_critical_task_id = critical_pending->id;
                }
            } else {
                last_critical_task_id = UNINITIALISED_TASK_ID;
            }
#undef WAKE_IF_SLEEPING
        }

        // Wake from sleep on any touch edge
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

        // Inactivity sleep (30 s with no touch)
        if ((now - last_activity_ms) >= UI_INACTIVITY_SLEEP_MS) {
            display_sleeping = true;
            display_backlight_set(false);
            corner_hold_active = false;
            sleep_triggered_this_hold = false;
            last_touch_pressed = pressed;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Top-right corner hold-to-sleep gesture
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
            last_pressed_x = x;
        }

        // --- Normal UI update ---
        ui_update_snapshot_from_system();
        ui_snapshot snap = UI_SNAPSHOT;

        expire_undo_buttons(display.dev_handle, now, snap);
        sync_main_task_state(display.dev_handle, snap, &prev_task_id);

        // Expire deny suppression once the current task has changed
        if (switch_denied_task_id.index != UINT16_MAX &&
                !task_id_equal(snap.task_id, active_task_id_during_switch)) {
            switch_denied_task_id        = UNINITIALISED_TASK_ID;
            active_task_id_during_switch = UNINITIALISED_TASK_ID;
        }

        // Reset overlay eligibility once the critical condition clears
        if (snap.critical_count == 0)
            switch_overlay_task_id = UNINITIALISED_TASK_ID;

        // Show confirmation overlay only for a newly escalated critical task —
        // never as a side-effect of user interaction completing the previous task.
        if (UI_MODE == UI_MODE_MAIN &&
            snap.critical_count > 0 &&
            !task_id_equal(snap.critical_task_id, switch_denied_task_id) &&
            !task_id_equal(snap.critical_task_id, switch_overlay_task_id)) {

            ui_enter_switch_prompt(display.dev_handle, snap);
        }

        if (pressed && !last_touch_pressed) {
            swipe_start_x = x;
            process_touch_down(display.dev_handle, x, y, snap, &PENDING_ACTION);
        }

        // Touch UP: execute pending action, or detect a swipe gesture
        if (!pressed && last_touch_pressed) {
            int16_t dx = (int16_t)last_pressed_x - (int16_t)swipe_start_x;
            bool is_swipe = (UI_MODE == UI_MODE_MAIN       && dx < -(int16_t)UI_SWIPE_THRESHOLD) ||
                            (UI_MODE == UI_MODE_TABLE_GRID && (dx >  (int16_t)UI_SWIPE_THRESHOLD ||
                                                               dx < -(int16_t)UI_SWIPE_THRESHOLD));
            if (is_swipe) {
                handle_swipe(display.dev_handle, dx, &PENDING_ACTION, &prev_task_id);
            } else if (PENDING_ACTION != UI_ACTION_NONE) {
                ui_action pact = PENDING_ACTION;
                PENDING_ACTION = UI_ACTION_NONE;
                dispatch_action(display.dev_handle, pact, now, &prev_task_id);
            }
        }

        last_touch_pressed = pressed;

        tick_periodic_updates(display.dev_handle, display_sleeping);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
