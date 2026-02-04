#include "../include/table_fsm.h"



static inline void enter_state(table_context *table, table_state next, time_ms current_time) {
    table->state = next;
    table->state_entered_at = current_time;
}


bool table_apply_event(table_context *table, fsm_transition_event event, time_ms current_time) {
    table_state prev = table->state;

    switch (table->state) {
        case TABLE_IDLE:
            if (event == EVENT_CUSTOMERS_SEATED) {
                enter_state(table, TABLE_SEATED, current_time);
            }
            break;

        case TABLE_SEATED:
            if (event == EVENT_MARK_COMPLETE || event == EVENT_TAKE_ORDER_EARLY_OR_REPEAT) {
                enter_state(table, TABLE_READY_FOR_ORDER, current_time);
            }
            break;

        case TABLE_READY_FOR_ORDER:
            if (event == EVENT_MARK_COMPLETE) {
                enter_state(table, TABLE_WAITING_FOR_ORDER, current_time);
            }
            break;

        case TABLE_WAITING_FOR_ORDER:
            if (event == EVENT_MARK_COMPLETE) {
                enter_state(table, TABLE_DINING, current_time);
            }
            break;

        case TABLE_DINING:
            if (event == TIMEOUT_PERIODIC_CHECKIN) {
                enter_state(table, TABLE_CHECKUP, current_time);
            }
            if (event == EVENT_TAKE_ORDER_EARLY_OR_REPEAT) {
                enter_state(table, TABLE_READY_FOR_ORDER, current_time);
            }
            break;

        case TABLE_CHECKUP:
            if (event == EVENT_TAKE_ORDER_EARLY_OR_REPEAT) {
                enter_state(table, TABLE_READY_FOR_ORDER, current_time);
            }
            if (event == EVENT_MARK_COMPLETE) {
                enter_state(table, TABLE_DINING, current_time);
            }
            if (event == EVENT_TABLE_CLOSED) {
                enter_state(table, TABLE_DONE, current_time);
            }
            break;

        case TABLE_DONE:
            if (event == EVENT_MARK_COMPLETE) {
                enter_state(table, TABLE_IDLE, current_time);
            }
            break;
    }

    return table->state != prev;
}


bool get_current_task_for_table(table_context *table, task_spec *out_task) {
    if (!table || !out_task) return false;

    task_kind kind;

    switch (table->state) {
        case TABLE_SEATED:
            kind = SERVE_WATER;
            break;

        case TABLE_READY_FOR_ORDER:
            kind = TAKE_ORDER;
            break;

        case TABLE_WAITING_FOR_ORDER:
            kind = SERVE_ORDER;
            break;

        case TABLE_DINING:
            return false;

        case TABLE_CHECKUP:
            kind = MONITOR_TABLE;
            break;

        case TABLE_DONE:
            kind = CLEAR_TABLE;
            break;

        case TABLE_IDLE:
            return false;

        default:
            return false;
    }

    out_task->table_number = table->table_number;
    out_task->task_kind = kind;
    return true;
}


void table_fsm_tick(table_context *table, time_ms current_time) {
    time_ms dt = current_time - table->state_entered_at;
    if (dt < 0) dt = 0;

    switch (table->state) {
        case TABLE_DINING:
            if (dt >= 1 * 60 * 1000) { // 1 min since entered dining (change to 10 min once done testing)
                table_apply_event(table, TIMEOUT_PERIODIC_CHECKIN, current_time);
            }
            break;

        default:
            break;
    }
}

