#ifndef TASK_DOMAIN_H
#define TASK_DOMAIN_H


#include <stdint.h>
#include "types.h"


#define SNOOZE_DURATION     30000       // Amount of time a task will be supressed when ignored in ms (30 seconds)

/* --------------- TASK PROPERTIES --------------- */
typedef enum {
    SERVE_WATER = 0,
    TAKE_ORDER,
    SERVE_ORDER,
    MONITOR_TABLE,
    CLEAR_TABLE,
} task_kind;

// Task base priorities
static const float TASK_BASE_PRIORITY[5] = {
    [SERVE_WATER]   = 5.0f,
    [TAKE_ORDER]    = 7.0f,
    [SERVE_ORDER]   = 8.0f,
    [MONITOR_TABLE] = 4.0f,
    [CLEAR_TABLE]   = 3.0f,
};

// Task time limits
static const time_ms TASK_TIME_LIMIT[5] = {
    [SERVE_WATER]   = 5 * 60 * 1000,
    [TAKE_ORDER]    = 4 * 60 * 1000,
    [SERVE_ORDER]   = 3 * 60 * 1000,
    [MONITOR_TABLE] = 10 * 60 * 1000,
    [CLEAR_TABLE]   = 10 * 60 * 1000,
};
/* --------------- --------------- --------------- */


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

    uint8_t table_number;
    task_kind kind;
} task;



/**
 * Mark a task as completed.
 *
 * Sets the task status to TASK_COMPLETED and clears any suppression state.
 * Once completed, the task should not be considered schedulable.
 *
 * This function is non-blocking and performs no delays or I/O.
 *
 * @param task Task to update.
 * @return SUCCESS on update, or TASK_DOES_NOT_EXIST if task is NULL.
 */
return_status task_mark_completed(task *task);


/**
 * Apply an "ignore" action to a task.
 *
 * Suppresses the task until current_time + SNOOZE_DURATION, increments the
 * ignore count (up to the configured limit), and marks the task as
 * TASK_SUPPRESSED. If the task exceeds the ignore limit, it is killed and
 * TASK_REMOVED is returned.
 *
 * This function is non-blocking and performs no RTOS delays or memory allocation.
 *
 * @param task Task to update.
 * @param current_time Current system time in milliseconds.
 * @return SUCCESS if suppressed, TASK_REMOVED if the task was killed due to
 *         repeated ignores, or TASK_DOES_NOT_EXIST if task is NULL.
 */
return_status task_apply_ignore(task *task, time_ms current_time);


/**
 * Refresh a task's time-dependent state.
 *
 * Transitions a suppressed task back to TASK_ELIGIBLE once the suppression
 * interval has elapsed. Callers typically invoke this periodically before
 * scheduling decisions are made.
 *
 * This function performs no RTOS delays and does not allocate memory.
 * It may emit log output when a task is unsuppressed; logging configuration
 * can add latency.
 *
 * @param task Task to refresh.
 * @param current_time Current system time in milliseconds.
 * @return SUCCESS on update, or TASK_DOES_NOT_EXIST if task is NULL.
 */
return_status refresh_task(task *task, time_ms current_time);


// /**
//  * Determine whether a task is currently schedulable.
//  *
//  * Returns false for killed or completed tasks. Returns true for eligible tasks.
//  * For suppressed tasks, returns true only if the suppression interval has elapsed.
//  *
//  * This function is pure and non-blocking.
//  *
//  * @param task Task to query.
//  * @param current_time Current system time in milliseconds.
//  * @return enum number corresponding to the status of the task.
//  */
// task_status is_task_schedulable(const task *task, time_ms current_time_ms);


/**
 * Kill a task and prevent it from being scheduled again.
 *
 * Sets the task status to TASK_KILLED and clears any suppression state.
 * A killed task is not schedulable.
 *
 * This function is non-blocking and performs no RTOS delays or I/O.
 *
 * @param task Task to kill.
 * @return SUCCESS on update, or TASK_DOES_NOT_EXIST if task is NULL.
 */
return_status kill_task(task *task);


/**
 * Initialise a task instance with default parameters for the given kind.
 *
 * Resets the task structure, assigns the provided identifier, and initialises
 * scheduling fields such as base priority, creation time, and due time using
 * the TASK_BASE_PRIORITY and TASK_TIME_LIMIT tables.
 *
 * This function is non-blocking, performs no RTOS delays, and does not allocate
 * memory. The task pointer must refer to valid writable storage.
 *
 * @param task Task instance to initialise.
 * @param id Identifier associated with the task (typically from task_pool_allocate()).
 * @param kind Task kind used to select default priority and time limits.
 * @param created_at Creation timestamp in milliseconds.
 * @param table Table number associated with the task.
 */
void task_init(task *task, task_id id, task_kind kind, time_ms created_at, uint8_t table);


const char *task_kind_to_str(task_kind kind);

const char *task_status_to_str(task_status status);


#endif // TASK_DOMAIN_H
