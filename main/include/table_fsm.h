#ifndef TABLE_FSM_H 
#define TABLE_FSM_H


#include "../include/types.h"
#include "../include/task_domain.h"
#include <stdint.h>



typedef enum {
    EVENT_MARK_COMPLETE,
    EVENT_TAKE_ORDER_EARLY_OR_REPEAT,
    EVENT_CUSTOMERS_SEATED,
    EVENT_TABLE_CLOSED,

    TIMEOUT_PERIODIC_CHECKIN,
} fsm_transition_event;


typedef enum {
    TABLE_IDLE,
    TABLE_SEATED,
    TABLE_READY_FOR_ORDER,
    TABLE_WAITING_FOR_ORDER,
    TABLE_DINING,
    TABLE_CHECKUP,
    TABLE_DONE,
} table_state;


typedef struct {
    uint8_t table_number;
    task_kind task_kind;
} task_spec;


typedef struct {
    uint8_t table_number;

    table_state state;
    time_ms state_entered_at;
} table_context;



/**
 * Derive the current task specification for a table based on its state.
 *
 * Maps the table's current FSM state to a corresponding task kind, if any.
 * States that do not emit tasks (e.g. IDLE or DINING) return false.
 *
 * This function does not mutate table state and does not allocate memory.
 * It is pure and non-blocking.
 *
 * @param table Table FSM context to query.
 * @param out_task Output task specification populated on success.
 * @return true if there is a valid and active task for the table, false otherwise.
 */
bool get_current_task_for_table(table_context *table, task_spec *out_task);


/**
 * Apply a transition event to a table finite state machine.
 *
 * Evaluates the given event against the table's current state and performs
 * a state transition if the event is valid in that state. On transition,
 * the table state and state-entry timestamp are updated.
 *
 * If the event does not result in a state change, the table state is left
 * unchanged.
 *
 * This function is non-blocking, performs no delays, and executes in bounded
 * time. It does not allocate memory or perform I/O.
 *
 * @param table Table FSM context to update.
 * @param event Transition event to apply.
 * @param current_time Current system time in milliseconds.
 * @return true if the table state changed as a result of the event; false otherwise.
 */
bool table_apply_event(table_context *table, fsm_transition_event event, time_ms current_time);


/**
 * Advance time-based behaviour of a table finite state machine.
 *
 * Evaluates time spent in the current state and triggers internal timeout
 * events when state-specific timing conditions are met (e.g. periodic
 * check-in while dining).
 *
 * This function may call table_apply_event() internally if a timeout
 * condition is satisfied.
 *
 * This function is non-blocking, performs no delays, and executes in bounded
 * time. It should be called periodically from a system tick or scheduler
 * context.
 *
 * @param table Table FSM context to update.
 * @param current_time Current system time in milliseconds.
 */
void table_fsm_tick(table_context *table, time_ms current_time);


#endif