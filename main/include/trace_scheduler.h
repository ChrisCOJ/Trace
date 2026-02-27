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
    USER_ACTION_CLOSE_TABLE,
} user_action;


/* ----------------------------- Structs ----------------------------- */

typedef struct {
    float urgency_weight;
    float age_weight;
    float base_priority_weight;
    float ignore_penalty_weight;

    float preempt_delta;                        // hysteresis threshold
    time_ms min_dwell_time_ms;                  // don’t switch too often
    /* 
    Increasing human state indicator will trigger an increase in min_dwell_time and 
    preempt delta by extra_dwell_ms_at_max_exhaustion * human_state_indicator and 
    extra_delta_at_max_exhaustion * human_state_indicator respectively.
    */
    time_ms extra_dwell_ms_at_max_exhaustion;
    float extra_delta_at_max_exhaustion;
} scheduler_config;


typedef struct {
    scheduler_config cfg;

    bool has_active_task;
    task_id active_task_id;
    time_ms task_active_since;

    float human_state_indicator;
} scheduler;



/* ----------------------------- Functions ----------------------------- */

/**
 * Initialise a scheduler instance and apply configuration defaults.
 *
 * Clears all scheduler state, copies the provided configuration (if non-NULL),
 * and applies built-in default values for any unset configuration fields.
 * Resets the human-state indicator and active-task tracking to a known state.
 *
 * This function performs no blocking I/O and does not sleep or delay.
 * It must be called before the scheduler is used (e.g. before scheduler_tick()
 * or scheduler_handle_action()).
 *
 * @param scheduler Pointer to the scheduler instance to initialise.
 * @param cfg Optional configuration. If NULL, defaults are used. If non-NULL,
 *            any zero-valued fields are replaced with defaults.
 */
void scheduler_init(scheduler *scheduler, const scheduler_config *config);


/**
 * Advance the scheduler by one tick and (re)select the active task.
 *
 * Scans the task pool for schedulable tasks, refreshes each candidate's
 * state, computes a utility score, and identifies the highest-scoring task.
 * If no active task exists, the best task is selected immediately. If an
 * active task exists, switching is governed by:
 *  - a minimum dwell time (to prevent rapid thrashing), and
 *  - a preemption delta/hysteresis margin (candidate must beat active by a margin),
 * both of which may be modulated by the human_state_indicator.
 *
 * This function is non-blocking in the sense that it performs no sleeping
 * or RTOS delays. Runtime cost is bounded and proportional to the task pool
 * capacity plus the cost of refresh_task()/is_task_schedulable().
 *
 * @note This function emits logs; depending on logging configuration, log I/O
 * may add latency. It should be called from a periodic task/timer context at a
 * rate appropriate for the system (e.g. hundreds of ms).
 *
 * @param scheduler_instance Scheduler instance to tick.
 * @param pool Task pool containing candidate tasks.
 * @param current_time Current system time in milliseconds.
 */
void scheduler_tick(scheduler *scheduler, task_pool *pool, time_ms current_time);


#endif