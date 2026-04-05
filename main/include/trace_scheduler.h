#ifndef TRACE_SCHEDULER_H
#define TRACE_SCHEDULER_H

#include "../include/types.h"
#include "../include/task_pool.h"
#include "../include/task_domain.h"

#include <stdbool.h>


typedef enum {
    USER_ACTION_NONE = 0,
    USER_ACTION_COMPLETE,
    USER_ACTION_START_TASK,
    USER_ACTION_IGNORE,
    USER_ACTION_TAKE_ORDER,
    USER_ACTION_BILL,
} user_action;


typedef struct {
    float urgency_weight;
    float age_weight;
    float base_priority_weight;
    float ignore_penalty_weight;

    float preempt_delta;            // margin a challenger's score must exceed the active task to prompt a switch
    time_ms min_dwell_time_ms;      // minimum time on the active task before a switch prompt can appear

    float zone_batch_bonus;         // bonus when next task is in the same zone as the current one
    float cross_zone_penalty;       // penalty when next task is in a different zone
} scheduler_config;


typedef struct {
    scheduler_config cfg;

    bool has_active_task;
    task_id active_task_id;
    time_ms task_active_since;

    uint8_t pending_count;          // eligible tasks excluding the active task
    uint8_t critical_count;         // pending tasks whose score exceeds active + preempt_delta
    task_id top_critical_id;        // highest-scoring critical pending task
} scheduler;


/**
 * Initialise a scheduler instance, applying defaults for any zero-valued config fields.
 */
void scheduler_init(scheduler *scheduler, const scheduler_config *config);


/**
 * Advance the scheduler by one tick.
 *
 * The active task changes only at natural breakpoints (no active task,
 * stale handle, or ineligible task).  The pending-task cache is refreshed
 * every tick; critical_count reflects tasks that exceed the switch-prompt
 * threshold relative to the current active task.
 */
void scheduler_tick(scheduler *scheduler_instance, task_pool *pool, time_ms current_time);


/**
 * Force a specific task to become active immediately.
 * Called when the operator confirms a switch via the UI overlay.
 */
void scheduler_force_active(scheduler *s, task_id id, time_ms now);


#endif
