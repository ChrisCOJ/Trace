#ifndef TRACE_SCHEDULER_H
#define TRACE_SCHEDULER_H

#include "../include/types.h"
#include "../include/task_pool.h"
#include "../include/task_domain.h"


#include <stdbool.h>



typedef enum {
    USER_ACTION_NONE = 0,
    USER_ACTION_COMPLETE,
    USER_ACTION_IGNORE
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

void scheduler_init(scheduler *scheduler, const scheduler_config *config);
void scheduler_tick(scheduler *scheduler, task_pool *pool, time_ms current_time);
void scheduler_handle_action(scheduler *scheduler, task_pool *pool, user_action action, time_ms current_time);


#endif