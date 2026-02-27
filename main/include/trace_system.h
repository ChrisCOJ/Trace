#ifndef TRACE_SYSTEM_H
#define TRACE_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>

#include "../include/types.h"
#include "../include/table_fsm.h"
#include "../include/task_pool.h"
#include "../include/trace_scheduler.h"


#define MAX_TABLES 28


/**
 * Initialise the trace system and all associated runtime state.
 *
 * Resets and initialises all table FSM instances to a known idle state,
 * clears any previously held scheduler state, and initialises the task
 * pool and scheduler using the provided configuration.
 *
 * This function must be called once during system startup before any
 * table events or scheduler ticks are processed.
 *
 * @param config Optional scheduler configuration. If NULL, default
 *               scheduler parameters are used.
 */
void trace_system_init(const scheduler_config *cfg);


/**
 * Apply a finite-state-machine event to a specific table and update
 * scheduler state if a transition occurs.
 *
 * Validates the table index, forwards the event to the table FSM, and
 * conditionally admits or updates tasks derived from the new table
 * state. If the event does not result in a state transition, no
 * scheduler-side effects occur.
 *
 * This function is the primary integration point between table state
 * changes and task admission logic.
 *
 * @param table_index Index of the table whose FSM should receive the event.
 * @param event       FSM transition event to apply.
 * @param now_ms      Current system time in milliseconds, used for
 *                    state timing and task admission.
 */
void system_apply_table_fsm_event(uint8_t table_index, fsm_transition_event ev, time_ms current_time_ms);


void system_take_order_now(uint8_t table_index, time_ms current_time_ms);


void system_close_table(uint8_t table_index, time_ms current_time_ms);


/**
 * Apply a user-initiated action to the scheduling system and propagate
 * any resulting domain state changes.
 *
 * @param shown_task         The task identifier of the task currently shown to the user on the UI
 * @param action             User action to apply (e.g. ignore, complete).
 * @param current_time       Current system time in milliseconds, used for
 *                           scheduling and state transitions.
 * @return                   
 */
bool system_apply_user_action_to_task(task_id shown_task_id, user_action action, time_ms current_time_ms);


table_state system_get_table_state(uint8_t table_index);


/**
 * Advance the trace system by one scheduling tick.
 *
 * Iterates over all table FSM instances, allowing each to perform any
 * time-based state transitions. If a table FSM changes state as a result
 * of the tick, tasks derived from the new state are admitted or updated
 * accordingly. After all table FSMs have been processed, the scheduler
 * is advanced to potentially select or update the active task.
 *
 * This function is intended to be called periodically from a timer or
 * scheduler context. It performs no blocking operations, does not sleep
 * or delay, and completes in bounded time proportional to the number of
 * tables and active tasks.
 *
 * @param current_time Current system time in milliseconds, used for
 *                     FSM timing and scheduler decisions.
 */
void trace_system_tick(time_ms current_time_ms);


// Read-only accessors for UI
const table_context *trace_system_get_table(uint8_t table_index);
task_id trace_system_get_active_task_id(void);
const task *trace_system_get_active_task(void);

#endif
