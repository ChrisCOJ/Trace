#include <string.h>
#include <stdbool.h>

#include "../include/trace_system.h"
#include "../include/table_fsm.h"
#include "../include/task_pool.h"
#include "../include/trace_scheduler.h"
#include "../include/touch_controller_util.h"

#include "esp_log.h"


table_context table_fsm_instances[MAX_TABLES];
static task_pool scheduler_task_pool;
static scheduler task_scheduler;

static const char *SYS_TAG = "SYS";



static inline bool is_valid_table_index(uint8_t table_index) {
    return table_index < MAX_TABLES;
}


// Admit the task implied by the table’s current FSM state (if any).
static void admit_task(const uint8_t table_number, time_ms current_time_ms) {
    if (!is_valid_table_index(table_number)) return;

    table_context *table_instance = &table_fsm_instances[table_number];

    task_spec current_task_spec;
    if (!get_current_task_for_table(table_instance, &current_task_spec)) {
        return;
    }

    task_pool_add(&scheduler_task_pool,
                 current_task_spec.table_number,
                 (task_kind)current_task_spec.task_kind,
                 current_time_ms);
}


// When a task is completed, advance the corresponding table FSM and admit the next task.
static void advance_table_fsm(const uint8_t table_number, time_ms current_time) {
    if (!is_valid_table_index(table_number)) return;

    // Progress the table FSM on completion.
    (void)table_apply_event(&table_fsm_instances[table_number], EVENT_MARK_COMPLETE, current_time);

    // Admit the next task for the new state (if any).
    admit_task(table_number, current_time);
}


// ----------------------------
// Public API
// ----------------------------

void trace_system_init(const scheduler_config *config) {
    memset(table_fsm_instances, 0, sizeof(table_fsm_instances));

    for (uint8_t table_index = 0; table_index < MAX_TABLES; table_index++) {
        table_fsm_instances[table_index].table_number = table_index; // enforce 0-based internally
        table_fsm_instances[table_index].state = TABLE_IDLE;
        table_fsm_instances[table_index].state_entered_at = 0;
    }

    task_pool_init(&scheduler_task_pool);
    scheduler_init(&task_scheduler, config);
    touch_init();
}


void system_apply_table_fsm_event(uint8_t table_index, fsm_transition_event event, time_ms current_time_ms) {
    if (!is_valid_table_index(table_index)) return;

    table_context *table_instance = &table_fsm_instances[table_index];

    bool did_state_change = table_apply_event(table_instance, event, current_time_ms);
    // Only admit a new task into the system if the table's state changed
    if (did_state_change) {
        admit_task(table_index, current_time_ms);
    }

    scheduler_tick(&task_scheduler, &scheduler_task_pool, current_time_ms);
}


void system_take_order_now(uint8_t table_index, time_ms current_time_ms) {
    system_apply_table_fsm_event(table_index, EVENT_TAKE_ORDER_EARLY_OR_REPEAT, current_time_ms);
}


void system_close_table(uint8_t table_index, time_ms current_time_ms) {
    system_apply_table_fsm_event(table_index, EVENT_TABLE_CLOSED, current_time_ms);
}


// void system_mark_task_completed(uint8_t table_index, time_ms current_time_ms) {
//     system_apply_table_fsm_event(table_index, EVENT_MARK_COMPLETE, current_time_ms);
// }


bool system_apply_user_action_to_task(task_id shown_task_id, user_action action, time_ms current_time_ms) {
    task *current_task = task_pool_get(&scheduler_task_pool, shown_task_id);
    if (!current_task) return false;  // Stale UI snapshot, ignore or force redraw

    // Save a copy of the current task to use for advancing the table fsm
    task task_snapshot = *current_task;

    // Keep task state up to date for schedulability checks / logging
    refresh_task(current_task, current_time_ms);

    // Block actions if the task is not schedulable
    if (current_task->status != TASK_ELIGIBLE) {
        ESP_LOGI(SYS_TAG, "action_blocked for task=%s (table=%u). Reason=%s",
                 task_kind_to_str(current_task->kind), (unsigned)current_task->table_number, task_status_to_str(current_task->status));

        // Recompute best suggestion so UI recovers quickly.
        scheduler_tick(&task_scheduler, &scheduler_task_pool, current_time_ms);
        return false;
    }

    // Apply action
    switch (action) {
        case USER_ACTION_COMPLETE:
            if (task_mark_completed(current_task)) {
                ESP_LOGE(SYS_TAG, "Task pointer invalid");
            }
            /* Use a task copy in case scheduler_tick() altered the task passed here. */
            advance_table_fsm(task_snapshot.table_number, current_time_ms);
            break;

        case USER_ACTION_IGNORE:
            ESP_LOGI(SYS_TAG, "IGNORE");
            task_apply_ignore(current_task, current_time_ms);
            break;  

        case USER_ACTION_TAKE_ORDER:  
            if (task_mark_completed(current_task)) {
                ESP_LOGE(SYS_TAG, "Task pointer invalid");
            }
            system_take_order_now(current_task->table_number, current_time_ms);
            break;    

        case USER_ACTION_CLOSE_TABLE:
            if (task_mark_completed(current_task)) {
                ESP_LOGE(SYS_TAG, "Task pointer invalid");
            }
            system_close_table(current_task->table_number, current_time_ms); 
            break;

        default: 
            ESP_LOGI(SYS_TAG, "DEFAULT");
            return false;
    }

    /* LOGS */
    ESP_LOGI(SYS_TAG,
        "task=%s (table=%u) status=%s ignore_count=%u suppress_until=%lu",
        task_kind_to_str(current_task->kind),
        current_task->table_number,
        task_status_to_str((unsigned)current_task->status),
        (unsigned)current_task->ignore_count,
        (unsigned long)current_task->suppress_until);
    /* LOGS */

    // Tick scheduler to pick next suggestion
    scheduler_tick(&task_scheduler, &scheduler_task_pool, current_time_ms);
    return true;
}


table_state system_get_table_state(uint8_t table_index) {
    if (table_index >= MAX_TABLES) return TABLE_IDLE; // safe fallback
    return table_fsm_instances[table_index].state;
}


void trace_system_tick(time_ms current_time_ms) {
    for (uint8_t table_index = 0; table_index < MAX_TABLES; table_index++) {
        table_state previous_state = table_fsm_instances[table_index].state;

        table_fsm_tick(&table_fsm_instances[table_index], current_time_ms);

        if (table_fsm_instances[table_index].state != previous_state) {
            admit_task(table_index, current_time_ms);
        }
    }

    scheduler_tick(&task_scheduler, &scheduler_task_pool, current_time_ms);
}


// ----------------------------
// Read-only accessors for UI/debugging
// ----------------------------

const table_context *trace_system_get_table(uint8_t table_index) {
    if (!is_valid_table_index(table_index)) return NULL;
    return &table_fsm_instances[table_index];
}

task_id trace_system_get_active_task_id(void) {
    return task_scheduler.active_task_id;
}

const task *trace_system_get_active_task(void) {
    if (!task_scheduler.has_active_task) return NULL;
    return task_pool_get_const(&scheduler_task_pool, task_scheduler.active_task_id);
}
