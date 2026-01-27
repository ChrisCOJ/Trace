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


void task_pool_init(task_pool *pool);

task_id task_pool_allocate(task_pool *pool);
void task_pool_free(task_pool *pool, task_id id);

task *task_pool_get(task_pool *pool, task_id id);
const task *task_pool_get_const(const task_pool *pool, task_id id);

bool is_task_id_valid(task_id id);



#endif