#include "../include/trace_scheduler.h"
#include "../include/task_domain.h"

#include <string.h>
#include <float.h>
#include "esp_log.h"


static const char *TAG = "trace_sched";


static float calculate_task_score(const scheduler *scheduler, const task *task, time_ms current_time);
static time_ms recompute_dwell_time(const scheduler *scheduler);
static float recompute_preempt_delta(const scheduler *scheduler);
static bool should_switch_task(const scheduler *scheduler, float active_task_score, float candidate_task_score, time_ms current_time);


void scheduler_init(scheduler *scheduler, const scheduler_config *cfg) {
    memset(scheduler, 0, sizeof(*scheduler));
    if (cfg) scheduler->cfg = *cfg;

    // Defaults
    if (scheduler->cfg.base_priority_weight == 0)  scheduler->cfg.base_priority_weight = 1.0f;
    if (scheduler->cfg.urgency_weight == 0)        scheduler->cfg.urgency_weight = 4.0f;
    if (scheduler->cfg.age_weight == 0)            scheduler->cfg.age_weight = 0.2f;
    if (scheduler->cfg.ignore_penalty_weight == 0) scheduler->cfg.ignore_penalty_weight = 1.0f;

    if (scheduler->cfg.preempt_delta == 0)         scheduler->cfg.preempt_delta = 0.8f;
    if (scheduler->cfg.min_dwell_time_ms == 0)     scheduler->cfg.min_dwell_time_ms = 20000; // 20s

    scheduler->human_state_indicator = 0.0f;
    scheduler->has_active_task = false;
    scheduler->task_active_since = 0;
    scheduler->active_task_id.index = UINT16_MAX;
    scheduler->active_task_id.generation = 0;
}


/* ------------------------- Scheduler tick recalculates score of each task ------------------------- */

void scheduler_tick(scheduler *scheduler_instance, task_pool *pool, time_ms current_time) {
    ESP_LOGD(TAG, "tick t=%lu", (unsigned long)current_time);

    task_id best_task_id = { .index = UINT16_MAX, .generation = 0 };
    float best_task_score = -FLT_MAX;

    /* Scan pool for best schedulable task */
    for (uint16_t index = 0; index < TASK_POOL_CAPACITY; ++index) {
        task_slot *slot = &pool->slots[index];
        if (slot->occupied == false) continue;

        task *candidate_task = &slot->task_instance;

        /* keep tasks up-to-date */
        refresh_task(candidate_task, current_time);

        if (!is_task_schedulable(candidate_task, current_time)) continue;

        float candidate_score =
            calculate_task_score(scheduler_instance, candidate_task, current_time);
        
        ESP_LOGD(TAG,
            "cand t=%lu id=(%u,%u) table=%u status=%u score=%.2f",
            (unsigned long)current_time,
            (unsigned)index, (unsigned)slot->generation,
            (unsigned)candidate_task->table,
            (unsigned)candidate_task->status,
            (double)candidate_score);
       

        if (candidate_score > best_task_score) {
            best_task_score = candidate_score;
            best_task_id.index = index;
            best_task_id.generation = slot->generation;

            ESP_LOGI(TAG,
                "best t=%lu id=(%u,%u) score=%.2f",
                (unsigned long)current_time,
                (unsigned)best_task_id.index, (unsigned)best_task_id.generation,
                (double)best_task_score);
       
        }
    }

    if (best_task_id.index == UINT16_MAX) {
        return;
    }

    if (!scheduler_instance->has_active_task) {
        scheduler_instance->has_active_task = true;
        scheduler_instance->active_task_id = best_task_id;
        scheduler_instance->task_active_since = current_time;

        ESP_LOGI(TAG,
            "select_initial t=%lu active=(%u,%u) score=%.2f",
            (unsigned long)current_time,
            (unsigned)best_task_id.index, (unsigned)best_task_id.generation,
            (double)best_task_score);
   
        return;
    }

    task *active_task = task_pool_get(pool, scheduler_instance->active_task_id);
    ESP_LOGI(TAG,
        "time=%lu current active task=(%u,%u) score=%.2f",
        (unsigned long)current_time,
        (unsigned)best_task_id.index, (unsigned)best_task_id.generation,
        (double)best_task_score);


    if (!active_task) {
        /* active handle stale: take best */
        ESP_LOGW(TAG,
            "active_stale t=%lu switching_to_best=(%u,%u) best_score=%.2f",
            (unsigned long)current_time,
            (unsigned)best_task_id.index, (unsigned)best_task_id.generation,
            (double)best_task_score);
   
        scheduler_instance->active_task_id = best_task_id;
        scheduler_instance->task_active_since = current_time;
        return;
    }

    refresh_task(active_task, current_time);

    if (!is_task_schedulable(active_task, current_time)) {
        scheduler_instance->active_task_id = best_task_id;
        scheduler_instance->task_active_since = current_time;
        return;
    }

    if (scheduler_instance->active_task_id.index == best_task_id.index &&
        scheduler_instance->active_task_id.generation == best_task_id.generation) {
        return;
    }

    float active_score =
        calculate_task_score(scheduler_instance, active_task, current_time);


        time_ms effective_dwell = recompute_dwell_time(scheduler_instance);
        time_ms dwell_elapsed = (time_ms)(current_time - scheduler_instance->task_active_since);
        float effective_delta = recompute_preempt_delta(scheduler_instance);
        
    ESP_LOGI(TAG,
                "decision t=%lu active=(%u,%u) a=%.2f best=(%u,%u) b=%.2f dwell=%lu/%lu delta=%.2f",
                (unsigned long)current_time,
                (unsigned)scheduler_instance->active_task_id.index,
                (unsigned)scheduler_instance->active_task_id.generation,
                (double)active_score,
                (unsigned)best_task_id.index,
                (unsigned)best_task_id.generation,
                (double)best_task_score,
                (unsigned long)dwell_elapsed,
                (unsigned long)effective_dwell,
                (double)effective_delta);
        
    if (should_switch_task(scheduler_instance, active_score, best_task_score, current_time)) {
        scheduler_instance->active_task_id = best_task_id;
        scheduler_instance->task_active_since = current_time;

        ESP_LOGI(TAG,
            "SWITCH t=%lu -> active=(%u,%u)",
            (unsigned long)current_time,
            (unsigned)best_task_id.index,
            (unsigned)best_task_id.generation);
   
    }
}



/* ------------------------- User actions ------------------------- */

void scheduler_handle_action(scheduler *scheduler, task_pool *pool, user_action action, time_ms current_time) {
    if (!scheduler->has_active_task) {
        return;
    }

    ESP_LOGI(TAG,
        "action t=%lu action=%d active=(%u,%u)",
        (unsigned long)current_time,
        (int)action,
        (unsigned)scheduler->active_task_id.index,
        (unsigned)scheduler->active_task_id.generation);


    task *active_task = task_pool_get(pool, scheduler->active_task_id);
    if (!active_task) {
        scheduler->has_active_task = false;
        return;
    }

    switch (action) {
        case USER_ACTION_COMPLETE:
            task_mark_completed(active_task);

            ESP_LOGI(TAG, "complete: status=%u", (unsigned)active_task->status);

            scheduler_tick(scheduler, pool, current_time);
            return;

        case USER_ACTION_IGNORE:
            task_apply_ignore(active_task, current_time);

            ESP_LOGI(TAG,
                "ignore: status=%u ignore_count=%u suppress_until=%lu",
                (unsigned)active_task->status,
                (unsigned)active_task->ignore_count,
                (unsigned long)active_task->suppress_until);
       
            scheduler_tick(scheduler, pool, current_time);
            return;

        case USER_ACTION_NONE:
        default:
            return;
    }
}



/* ----------------------------- Internals ----------------------------- */

static float calculate_task_score(const scheduler *scheduler_instance, const task *task_instance, time_ms current_time) {
    float base_priority = (float)task_instance->base_priority;

    float overdue_ms =
        (current_time > task_instance->time_limit) ?
        (float)(current_time - task_instance->time_limit) : 0.0f;

    float urgency = overdue_ms / 60000.0f;
    if (urgency > 10.0f) urgency = 10.0f;

    float age_ms =
        (current_time > task_instance->created_at) ?
        (float)(current_time - task_instance->created_at) : 0.0f;

    float age = age_ms / 120000.0f;
    if (age > 7.0f) age = 7.0f;

    float ignore_penalty = (float)task_instance->ignore_count;

    float score = scheduler_instance->cfg.base_priority_weight * base_priority +
                  scheduler_instance->cfg.urgency_weight * urgency +
                  scheduler_instance->cfg.age_weight * age -
                  scheduler_instance->cfg.ignore_penalty_weight * ignore_penalty;

    ESP_LOGI(TAG,
        "id=%u bp=%.3f now=%ld created=%ld due=%ld age_ms=%ld overdue_ms=%ld age=%.3f urg=%.3f score=%.3f",
        task_instance->id.index,
        task_instance->base_priority,
        (long)current_time,
        (long)task_instance->created_at,
        (long)task_instance->time_limit,
        (long)((current_time > task_instance->created_at) ? (current_time - task_instance->created_at) : 0),
        (long)((current_time > task_instance->time_limit) ? (current_time - task_instance->time_limit) : 0),
        age,
        urgency,
        score
    );
    
    return score;
}


static time_ms recompute_dwell_time(const scheduler *scheduler) {
    time_ms effective_min_dwell_time = scheduler->cfg.min_dwell_time_ms + 
                                       scheduler->cfg.extra_dwell_ms_at_max_exhaustion *
                                       scheduler->human_state_indicator;

    return effective_min_dwell_time;
}


static float recompute_preempt_delta(const scheduler *scheduler) {
    float effective_preempt_delta = scheduler->cfg.preempt_delta + 
                                    scheduler->cfg.extra_delta_at_max_exhaustion * 
                                    scheduler->human_state_indicator;

    return effective_preempt_delta;
}


static bool should_switch_task(const scheduler *scheduler, float active_task_score, float candidate_task_score, time_ms current_time) {
    time_ms effective_min_dwell_time = recompute_dwell_time(scheduler);
    if ((time_ms)(current_time - scheduler->task_active_since) < effective_min_dwell_time) {
        ESP_LOGI(TAG, "block: dwell");
        return false;
    }

    float effective_preempt_delta = recompute_preempt_delta(scheduler);
    
    if (!(candidate_task_score > active_task_score + effective_preempt_delta)) {
        ESP_LOGI(TAG, "block: margin");
        return false;
    }
    
    ESP_LOGI(TAG, "allow: switch");
    return true;
}



