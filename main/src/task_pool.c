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
