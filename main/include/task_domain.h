#ifndef TASK_DOMAIN_H
#define TASK_DOMAIN_H


#include <stdint.h>
#include "types.h"


#define SNOOZE_DURATION     30000       // Amount of time a task will be supressed when ignored


/* ENUMS */
typedef enum {
    TASK_ELIGIBLE,
    TASK_SUPPRESSED,
    TASK_COMPLETED,
    TASK_KILLED,
} task_status;


typedef enum {
    SUCCESS = 0,
    TASK_DOES_NOT_EXIST,
    TASK_REMOVED,
} return_status;


/* STRUCTS */
typedef struct {
    uint16_t index;
    uint16_t generation;
} task_id;


typedef struct {
    task_id id;
    task_status status;
    float base_priority;

    time_ms time_limit;
    time_ms suppress_until;
    time_ms created_at;

    uint8_t ignore_count;

    uint8_t table;
} task;


return_status task_mark_completed(task *task);
return_status task_apply_ignore(task *task, time_ms current_time);
return_status refresh_task(task *task, time_ms current_time);
bool is_task_schedulable(const task *task, time_ms current_time_ms);
return_status kill_task(task *task);
// Testing
void task_init(task *task, task_id id, float base_priority, time_ms created_at, time_ms time_limit, uint8_t table);


#endif 