#include "../include/task_domain.h"
#include <string.h>
#include "esp_log.h"


static const char *TAG = "task_domain";


// Testing
void task_init(task *task, task_id id, float base_priority, time_ms created_at, time_ms time_limit, uint8_t table) {
    memset(task, 0, sizeof(*task));
    task->id = id;
    task->status = TASK_ELIGIBLE;
    task->base_priority = base_priority;
    task->created_at = created_at;
    task->time_limit = time_limit;
    task->suppress_until = 0;
    task->ignore_count = 0;
    task->table = table;
}
// Testing


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


return_status refresh_task(task *task, time_ms current_time) {
    if (!task) return TASK_DOES_NOT_EXIST;

    if (task->status == TASK_SUPPRESSED && current_time >= task->suppress_until) {
        ESP_LOGI(TAG,
                    "unsuppress t=%lu task=(%u,%u) table=%u",
                    (unsigned long)current_time,
                    (unsigned)task->id.index, (unsigned)task->id.generation,
                    (unsigned)task->table);

        task->status = TASK_ELIGIBLE;
            
        task->status = TASK_ELIGIBLE;
    }

    return SUCCESS;
}


bool is_task_schedulable(const task *task, time_ms current_time) {
    if (!task) return false;

    if (task->status == TASK_KILLED) return false;
    if (task->status == TASK_COMPLETED) return false;

    if (task->status == TASK_SUPPRESSED) {
        return current_time >= task->suppress_until;
    }

    return task->status == TASK_ELIGIBLE;
}


return_status kill_task(task *task) {
    if (!task) return TASK_DOES_NOT_EXIST;

    task->status = TASK_KILLED;
    task->suppress_until = 0;

    return SUCCESS;
}