#ifndef TASK_POOL_H
#define TASK_POOL_H


#include <stdint.h>
#include <stdbool.h>

#include "task_domain.h"


#define TASK_POOL_CAPACITY           32


typedef struct {
    task task_instance;
    uint16_t generation;
    bool occupied;
} task_slot;

typedef struct {
    task_slot slots[TASK_POOL_CAPACITY];
} task_pool;




/**
 * Initialise a task pool and reset all slot state.
 *
 * Clears the task pool, marks all slots as unoccupied, and resets generation
 * counters. Any previously allocated task identifiers become invalid after
 * this call.
 *
 * This function performs no blocking operations and does not allocate memory.
 * It must be called before any other task_pool_* function is used.
 *
 * @param pool Task pool to initialise.
 */
void task_pool_init(task_pool *pool);


/**
 * Allocate a free slot from the task pool.
 *
 * Searches for an unoccupied slot, marks it as occupied, and returns a
 * generation-stamped task identifier. The task contents are not initialised
 * by this function and must be set by the caller.
 *
 * If no free slot is available, an invalid task identifier is returned.
 *
 * This function is non-blocking and performs a bounded linear scan of the pool.
 *
 * @param pool Task pool to allocate from.
 * @return Task identifier for the allocated slot, or an invalid ID on failure.
 */
task_id task_pool_allocate(task_pool *pool);


/**
 * Free a previously allocated task slot.
 *
 * Marks the specified slot as unoccupied and increments its generation counter
 * to invalidate any stale task identifiers referencing the same index.
 *
 * If the identifier is invalid, stale, or refers to an unoccupied slot, this
 * function has no effect.
 *
 * This function is non-blocking and performs no memory allocation.
 *
 * @param pool Task pool containing the slot.
 * @param id Task identifier to free.
 */
void task_pool_free(task_pool *pool, task_id id);


/**
 * Retrieve a mutable task instance by identifier.
 *
 * Returns a pointer to the task associated with the given identifier only if
 * the slot is currently occupied and the generation matches. Stale or invalid
 * identifiers result in NULL.
 *
 * This function is non-blocking and performs no mutation.
 *
 * @param pool Task pool containing the task.
 * @param id Task identifier.
 * @return Pointer to the task instance, or NULL if invalid or stale.
 */
task *task_pool_get(task_pool *pool, task_id id);


// Const-qualified variant of task_pool_get().
const task *task_pool_get_const(const task_pool *pool, task_id id);


/**
 * Check whether a task identifier refers to a valid pool index.
 *
 * This function validates only the index range; it does not check whether
 * the slot is currently occupied or whether the generation matches.
 *
 * This function is pure and non-blocking.
 *
 * @param id Task identifier to validate.
 * @return true if the identifier index is within pool bounds.
 */
bool is_task_id_valid(task_id id);


/**
 * Find an existing active task matching a logical key.
 *
 * Searches the task pool for an occupied task matching the given table number
 * and task kind. Tasks that are completed or killed are ignored.
 *
 * This function is used to prevent duplicate tasks representing the same
 * logical unit of work.
 *
 * This function is non-blocking and performs a bounded linear scan of the pool.
 *
 * @param pool Task pool to search.
 * @param table_number Table identifier associated with the task.
 * @param kind Task kind.
 * @return Task identifier if a matching active task exists, otherwise an
 *         invalid task identifier.
 */
task_id task_pool_find_by_key(const task_pool *pool, uint8_t table_number, task_kind kind);


/**
 * Add or update a task in the pool based on a logical key.
 *
 * If an active task matching the given table number and task kind already
 * exists, that task is updated with current default parameters and returned.
 * If a completed or killed version exists, it is freed and replaced.
 *
 * If no matching task exists, a new task slot is allocated and initialised.
 * If the pool is full, an invalid task identifier is returned.
 *
 * This function performs no blocking operations and executes in bounded time
 * proportional to the task pool capacity.
 *
 * @param pool Task pool to modify.
 * @param table_number Table identifier associated with the task.
 * @param kind Task kind.
 * @param now Current system time in milliseconds, used for task initialisation.
 * @return Task identifier for the added or updated task, or an invalid ID on failure.
 */
task_id task_pool_add(task_pool *pool, uint8_t table_number, task_kind kind, time_ms now);




#endif