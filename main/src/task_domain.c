#include "../include/task_domain.h"
#include <string.h>
#include "esp_log.h"


static const char *TAG = "task_domain";

extern task_id INVALID_TASK_ID = { .index = UINT16_MAX, .generation = 0 };


void task_init(task *task, task_id id, task_kind kind, time_ms created_at, uint8_t table) {
    memset(task, 0, sizeof(*task));
    task->id = id;
    task->status = TASK_ELIGIBLE;
    task->base_priority = TASK_BASE_PRIORITY[kind];
    task->created_at = created_at;
    task->time_limit = TASK_TIME_LIMIT[kind] + created_at;
    task->suppress_until = 0;
    task->ignore_count = 0;
    task->table_number = table;
    task->kind = kind;
}


return_status task_mark_completed(task *task) {
    if (!task) return TASK_DOES_NOT_EXIST;
    
    task->status = TASK_COMPLETED;
    task->suppress_until = 0;
    
    return SUCCESS;
}


return_status task_apply_ignore(task *task, time_ms current_time) {
    if (!task) return TASK_DOES_NOT_EXIST;

    task->suppress_until = current_time + SNOOZE_DURATION;

    if (task->ignore_count < 3) {
        task->ignore_count++;
    }
    else {
        // Kill the task if ignored more than 3 times
        kill_task(task);
        return TASK_REMOVED;
    }
    task->status = TASK_SUPPRESSED;

    return SUCCESS;
}


return_status task_undo_ignore(task *task, uint8_t prev_ignore_count, time_ms prev_suppress_until) {
    if (!task) return TASK_DOES_NOT_EXIST;
    task->ignore_count   = prev_ignore_count;
    task->suppress_until = prev_suppress_until;
    task->status         = TASK_ELIGIBLE;
    return SUCCESS;
}


return_status refresh_task(task *task, time_ms current_time) {
    if (!task) return TASK_DOES_NOT_EXIST;

    if (task->status == TASK_SUPPRESSED && current_time >= task->suppress_until) {
        ESP_LOGI(TAG,
                    "unsuppress t=%lu task=(%u,%u) table=%u",
                    (unsigned long)current_time,
                    (unsigned)task->id.index, (unsigned)task->id.generation,
                    (unsigned)task->table_number);
            
        task->status = TASK_ELIGIBLE;
    }

    return SUCCESS;
}


return_status kill_task(task *task) {
    if (!task) return TASK_DOES_NOT_EXIST;

    task->status = TASK_KILLED;
    task->suppress_until = 0;

    return SUCCESS;
}


const char *task_kind_to_str(task_kind kind) {
    switch (kind) {
        case SERVE_WATER:   return "SERVE WATER";
        case TAKE_ORDER:    return "TAKE ORDER";
        case PREPARE_ORDER: return "PREPARE ORDER";
        case SERVE_ORDER:   return "SERVE ORDER";
        case MONITOR_TABLE: return "MONITOR TABLE";
        case PRESENT_BILL:  return "PRESENT BILL";
        case CLEAR_TABLE:   return "CLEAR TABLE";
        default:            return "UNKNOWN";
    }
}


const char *task_status_to_str(task_status status) {
    switch (status) {
        case TASK_ELIGIBLE:     return "TASK_ELIGIBLE";
        case TASK_SUPPRESSED:   return "TASK_SUPRESSED";
        case TASK_COMPLETED:    return "TASK_COMPLETED";
        case TASK_KILLED:       return "TASK_KILLED";
        default:                return "UNKNOWN";
    }
}