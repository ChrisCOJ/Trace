#include <string.h>
#include <stdbool.h>

#include "../include/trace_system.h"
#include "../include/table_fsm.h"
#include "../include/task_pool.h"
#include "../include/trace_scheduler.h"
#include "../include/touch_controller_util.h"

#include "esp_log.h"


#define DEBUG_STALE_STATE


table_context table_fsm_instances[MAX_TABLES];
static task_pool scheduler_task_pool;
static scheduler task_scheduler;

static const char *SYS_TAG = "SYS";


#ifdef DEBUG_STALE_STATE
static void debug_validate_system_state(void) {
    for (uint8_t table = 0; table < MAX_TABLES; ++table) {
        task_kind expected = system_get_current_task_kind_for_table(table);
        task *actual = system_get_current_task_pointer_for_table(table);

        if (expected == TASK_NOT_APPLICABLE) {
            if (actual) {
                ESP_LOGE(SYS_TAG,
                    "INVARIANT FAIL: table=%u state=%d expects no task but found %s status=%s",
                    table,
                    table_fsm_instances[table].state,
                    task_kind_to_str(actual->kind),
                    task_status_to_str(actual->status));
            }
        } else {
            if (!actual) {
                ESP_LOGE(SYS_TAG,
                    "INVARIANT FAIL: table=%u state=%d expects task=%s but found none",
                    table,
                    table_fsm_instances[table].state,
                    task_kind_to_str(expected));
            } else if (actual->kind != expected) {
                ESP_LOGE(SYS_TAG,
                    "INVARIANT FAIL: table=%u state=%d expects task=%s but found %s status=%s",
                    table,
                    table_fsm_instances[table].state,
                    task_kind_to_str(expected),
                    task_kind_to_str(actual->kind),
                    task_status_to_str(actual->status));
            } else if (actual->status != TASK_ELIGIBLE && actual->status != TASK_SUPPRESSED) {
                ESP_LOGE(SYS_TAG,
                    "INVARIANT FAIL: table=%u state=%d expected task=%s has bad status=%s",
                    table,
                    table_fsm_instances[table].state,
                    task_kind_to_str(actual->kind),
                    task_status_to_str(actual->status));
            }
        }
    }

    if (task_scheduler.has_active_task) {
        const task *active = task_pool_get_const(&scheduler_task_pool, task_scheduler.active_task_id);
        if (!active) {
            ESP_LOGE(SYS_TAG, "INVARIANT FAIL: scheduler active task id is stale");
        } else if (active->status != TASK_ELIGIBLE) {
            ESP_LOGE(SYS_TAG, "INVARIANT FAIL: scheduler active task %s table=%u status=%s",
                task_kind_to_str(active->kind),
                active->table_number,
                task_status_to_str(active->status));
        }
    }
}


static void debug_log_pool_usage(void) {
    uint16_t occupied = 0;
    uint16_t eligible = 0;
    uint16_t suppressed = 0;
    uint16_t completed = 0;
    uint16_t killed = 0;

    for (uint16_t i = 0; i < TASK_POOL_CAPACITY; ++i) {
        task_slot *slot = &scheduler_task_pool.slots[i];
        if (!slot->occupied) {
            continue;
        }

        occupied++;

        switch (slot->task_instance.status) {
            case TASK_ELIGIBLE:   eligible++; break;
            case TASK_SUPPRESSED: suppressed++; break;
            case TASK_COMPLETED:  completed++; break;
            case TASK_KILLED:     killed++; break;
            default: break;
        }
    }

    ESP_LOGI(SYS_TAG,
             "POOL usage: occupied=%u eligible=%u suppressed=%u completed=%u killed=%u capacity=%u",
             occupied, eligible, suppressed, completed, killed, TASK_POOL_CAPACITY);
}
#endif


static inline bool is_valid_table_index(uint8_t table_index) {
    return table_index < MAX_TABLES;
}


// Kill all non-terminal tasks for a table so stale tasks don’t compete with
// the task implied by the table’s new FSM state.
static void kill_tasks_for_table(uint8_t table_number) {
    for (uint16_t i = 0; i < TASK_POOL_CAPACITY; ++i) {
        task_slot *slot = &scheduler_task_pool.slots[i];
        if (!slot->occupied) continue;

        task *t = &slot->task_instance;
        if (t->table_number == table_number &&
            t->status != TASK_KILLED && t->status != TASK_COMPLETED) {
            kill_task(t);
            ESP_LOGI(SYS_TAG, "killed stale %s task (table=%u) on FSM transition",
                     task_kind_to_str(t->kind), (unsigned)table_number);
        }
    }
}


// Admit the task implied by the table’s current FSM state (if any).
static void admit_task(const uint8_t table_number, time_ms current_time_ms) {
    if (!is_valid_table_index(table_number)) {
        return;
    }

    task_kind kind = system_get_current_task_kind_for_table(table_number);
    if (kind == TASK_NOT_APPLICABLE) {
        return;
    }

    task_id id = task_pool_add(&scheduler_task_pool, table_number, kind, current_time_ms);
    if (id.index == UINT16_MAX) {
        ESP_LOGE(SYS_TAG, "admit_task FAILED: table=%u state=%d kind=%s",
                 (unsigned)table_number,
                 (int)table_fsm_instances[table_number].state,
                 task_kind_to_str(kind));

        #ifdef DEBUG_STALE_STATE
        debug_log_pool_usage();
        #endif

        return;
    }
}


static void reap_dead_tasks(void) {
    for (uint16_t i = 0; i < TASK_POOL_CAPACITY; ++i) {
        task_slot *slot = &scheduler_task_pool.slots[i];
        if (!slot->occupied) {
            continue;
        }

        task *task_inst = &slot->task_instance;
        if (task_inst->status == TASK_COMPLETED || task_inst->status == TASK_KILLED) {
            task_id id = { .index = i, .generation = slot->generation };
            task_pool_free(&scheduler_task_pool, id);
        }
    }
}


// Advance the table FSM on task completion and queue the next task.
static void advance_table_fsm(const uint8_t table_number, time_ms current_time) {
    if (!is_valid_table_index(table_number)) {
        return;
    }

    table_context *table = &table_fsm_instances[table_number];
    bool did_state_change = table_apply_event(table, EVENT_MARK_COMPLETE, current_time);

    if (did_state_change) {
        kill_tasks_for_table(table_number);
        reap_dead_tasks();
        admit_task(table_number, current_time);
    }
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
    if (!is_valid_table_index(table_index)) {
        ESP_LOGE(SYS_TAG, "Invalid table index");
        return;
    }

    table_context *table_instance = &table_fsm_instances[table_index];

    bool did_state_change = table_apply_event(table_instance, event, current_time_ms);
    if (did_state_change) {
        kill_tasks_for_table(table_index);
        reap_dead_tasks();
        admit_task(table_index, current_time_ms);
    }

    scheduler_tick(&task_scheduler, &scheduler_task_pool, current_time_ms);

    #ifdef DEBUG_STALE_STATE
    debug_validate_system_state();
    #endif
}


bool system_apply_user_action_to_task(task_id shown_task_id, user_action action, time_ms current_time_ms) {
    task *current_task = task_pool_get(&scheduler_task_pool, shown_task_id);
    if (!current_task) {
        ESP_LOGE(SYS_TAG, "NULL current_task");
        return false;  // Stale UI snapshot, ignore or force redraw
    }

    task task_snapshot = *current_task;

    refresh_task(current_task, current_time_ms);

    if (current_task->status != TASK_ELIGIBLE) {
        ESP_LOGI(SYS_TAG, "action_blocked for task=%s (table=%u). Reason=%s",
                 task_kind_to_str(current_task->kind), (unsigned)current_task->table_number, task_status_to_str(current_task->status));

        // Recompute best suggestion so UI recovers quickly.
        scheduler_tick(&task_scheduler, &scheduler_task_pool, current_time_ms);

        #ifdef DEBUG_STALE_STATE
        debug_validate_system_state();
        #endif

        return false;
    }

    // Apply action
    switch (action) {
        case USER_ACTION_COMPLETE:
            if (task_mark_completed(current_task)) {
                ESP_LOGE(SYS_TAG, "Task pointer invalid");
            }
            // Use a task copy in case scheduler_tick() altered the task passed here.
            task_pool_free(&scheduler_task_pool, task_snapshot.id);
            advance_table_fsm(task_snapshot.table_number, current_time_ms);
            break;

        case USER_ACTION_IGNORE:
            ESP_LOGI(SYS_TAG, "IGNORE");
            task_apply_ignore(current_task, current_time_ms);
            break;  

        default: 
            ESP_LOGI(SYS_TAG, "DEFAULT");
            return false;
    }
    scheduler_tick(&task_scheduler, &scheduler_task_pool, current_time_ms);

    #ifdef DEBUG_STALE_STATE
    debug_validate_system_state();
    #endif

    return true;
}


bool system_undo_task_ignore(task_id id, uint8_t prev_ignore_count, time_ms prev_suppress_until, time_ms now) {
    task *t = task_pool_get(&scheduler_task_pool, id);
    if (!t) {
        ESP_LOGE(SYS_TAG, "system_undo_task_ignore: task slot expired");
        return false;
    }
    task_undo_ignore(t, prev_ignore_count, prev_suppress_until);
    ESP_LOGI(SYS_TAG, "UNDO IGNORE task=%s (table=%u)", task_kind_to_str(t->kind), (unsigned)t->table_number);
    scheduler_tick(&task_scheduler, &scheduler_task_pool, now);
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
            kill_tasks_for_table(table_index);
            reap_dead_tasks();
            admit_task(table_index, current_time_ms);
        }
    }
    // Loop the task pool and clean up dead tasks
    reap_dead_tasks();

    scheduler_tick(&task_scheduler, &scheduler_task_pool, current_time_ms);

    #ifdef DEBUG_STALE_STATE
    debug_validate_system_state();
    #endif
}


// ----------------------------
// Read-only accessors for UI/debugging
// ----------------------------

const table_context *system_get_table(uint8_t table_index) {
    if (!is_valid_table_index(table_index)) return NULL;
    return &table_fsm_instances[table_index];
}


task_kind system_get_current_task_kind_for_table(uint8_t table_index) {
    task_kind kind;
    const table_context *table = system_get_table(table_index);
    if (!table) {
        ESP_LOGE(SYS_TAG, "system_get_current_task_kind_for_table() got a null table pointer, can't continue.");
        return TASK_NOT_APPLICABLE;
    }

    switch (table->state){
        case TABLE_SEATED:
            kind = SERVE_WATER;
            break;

        case TABLE_READY_FOR_ORDER:
            kind = TAKE_ORDER;
            break;

        case TABLE_PLACED_ORDER:
            kind = PREPARE_ORDER;
            break;

        case TABLE_WAITING_FOR_ORDER:
            kind = SERVE_ORDER;
            break;

        case TABLE_DINING:
            kind = TASK_NOT_APPLICABLE;
            break;

        case TABLE_CHECKUP:
            kind = MONITOR_TABLE;
            break;

        case TABLE_REQUESTED_BILL:
            kind = PRESENT_BILL;
            break;

        case TABLE_DONE:
            kind = CLEAR_TABLE;
            break;

        case TABLE_IDLE:
            kind = TASK_NOT_APPLICABLE;
            break;

        default:
            kind = TASK_NOT_APPLICABLE;
            break;
    }

    return kind;
}


task *system_get_current_task_pointer_for_table(uint8_t table_index) {
    task_kind kind = system_get_current_task_kind_for_table(table_index);

    task_id id = task_pool_find_by_key(&scheduler_task_pool, table_index, kind);
    if (id.index == INVALID_TASK_ID.index && id.generation == INVALID_TASK_ID.generation) return NULL;

    task *t = task_pool_get(&scheduler_task_pool, id);
    if (!t) return NULL;

    return t;
}


task_id system_get_active_task_id(void) {
    return task_scheduler.active_task_id;
}

const task *system_get_active_task(void) {
    if (!task_scheduler.has_active_task) return NULL;
    return task_pool_get_const(&scheduler_task_pool, task_scheduler.active_task_id);
}

uint8_t system_get_pending_count(void) {
    return task_scheduler.pending_count;
}

uint8_t system_get_critical_pending_count(void) {
    return task_scheduler.critical_count;
}

const task *system_get_top_critical_task(void) {
    if (task_scheduler.critical_count == 0) return NULL;
    return task_pool_get_const(&scheduler_task_pool, task_scheduler.top_critical_id);
}

void system_force_active_task(task_id id, time_ms now) {
    task *task_inst = task_pool_get(&scheduler_task_pool, id);
    if (!task_inst) {
        ESP_LOGE(SYS_TAG, "force_active rejected: stale id");
        return;
    }
    if (task_inst->status != TASK_ELIGIBLE) {
        ESP_LOGE(SYS_TAG, "force_active rejected: task not eligible (%s)",
                 task_status_to_str(task_inst->status));
        return;
    }

    scheduler_force_active(&task_scheduler, id, now);
}
