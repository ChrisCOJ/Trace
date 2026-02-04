#include "../include/task_pool.h"

#include <string.h>



bool is_task_id_valid(task_id id) {
    return id.index < TASK_POOL_CAPACITY;
}


void task_pool_init(task_pool *pool) {
    if (!pool) return;

    memset(pool, 0, sizeof(*pool));
    for (uint16_t index = 0; index < TASK_POOL_CAPACITY; ++index) {
        pool->slots[index].occupied = false;
        pool->slots[index].generation = 0;
    }
}


task_id task_pool_allocate(task_pool *pool) {
    task_id invalid = { .index = UINT16_MAX, .generation = 0 };
    if (!pool) return invalid;

    for (uint16_t index = 0; index < TASK_POOL_CAPACITY; ++index) {
        task_slot *slot = &pool->slots[index];

        if (slot->occupied == false) {
            slot->occupied = true;

            task_id id = {
                .index = index,
                .generation = slot->generation
            };
            return id;
        }
    }

    return invalid;
}


void task_pool_free(task_pool *pool, task_id id) {
    if (!pool) return;
    if (!is_task_id_valid(id)) return;

    task_slot *slot = &pool->slots[id.index];

    if (slot->occupied == false) return;
    if (slot->generation != id.generation) return;

    slot->occupied = false;
    slot->generation++; /* invalidate stale handles */
}


task *task_pool_get(task_pool *pool, task_id id) {
    if (!pool) return NULL;
    if (!is_task_id_valid(id)) return NULL;

    task_slot *slot = &pool->slots[id.index];

    if (slot->occupied != true) return NULL;
    if (slot->generation != id.generation) return NULL;

    return &slot->task_instance;
}


const task *task_pool_get_const(const task_pool *pool, task_id id) {
    if (!pool) return NULL;
    if (!is_task_id_valid(id)) return NULL;

    const task_slot *slot = &pool->slots[id.index];

    if (slot->occupied != true) return NULL;
    if (slot->generation != id.generation) return NULL;

    return &slot->task_instance;
}


static inline task_id invalid_task_id(void) {
    task_id id = { .index = UINT16_MAX, .generation = 0 };
    return id;
}


task_id task_pool_find_by_key(const task_pool *pool, uint8_t table_number, task_kind kind) {
    if (!pool) return invalid_task_id();

    for (uint16_t i = 0; i < TASK_POOL_CAPACITY; ++i) {
        const task_slot *slot = &pool->slots[i];
        if (!slot->occupied) continue;

        const task *task = &slot->task_instance;

        // Only treat it as “existing” if it’s still relevant
        if (task->table_number == table_number && task->kind == kind &&
            task->status != TASK_KILLED && task->status != TASK_COMPLETED) {

            task_id id = { .index = i, .generation = slot->generation };
            return id;
        }
    }
    return invalid_task_id();
}


task_id task_pool_add(task_pool *pool, uint8_t table_number, task_kind kind, time_ms now) {
    if (!pool) return invalid_task_id();

    // If a relevant task already exists, update it and return it.
    task_id existing = task_pool_find_by_key(pool, table_number, kind);
    if (existing.index != UINT16_MAX) {
        task *task = task_pool_get(pool, existing);
        if (task) {
            // Update “spec defaults".
            task->base_priority = TASK_BASE_PRIORITY[kind];
            task->time_limit = now + TASK_TIME_LIMIT[kind];
        }
        return existing;
    }

    // If a dead version exists (completed/killed), free it so we can reuse the slot.
    for (uint16_t i = 0; i < TASK_POOL_CAPACITY; ++i) {
        task_slot *slot = &pool->slots[i];
        if (!slot->occupied) continue;

        task *task = &slot->task_instance;
        if (task->table_number == table_number && task->kind == kind &&
            (task->status == TASK_KILLED || task->status == TASK_COMPLETED)) {

            task_id dead_task = { .index = i, .generation = slot->generation };
            task_pool_free(pool, dead_task);
            break;
        }
    }

    // Allocate a new slot and initialise
    task_id id = task_pool_allocate(pool);
    if (id.index == UINT16_MAX) return id;

    task *task = task_pool_get(pool, id);
    if (!task) return invalid_task_id();

    task_init(task, id,
              kind,
              now,
              table_number);

    return id;
}

