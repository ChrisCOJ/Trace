#include "../include/trace_scheduler.h"
#include "../include/task_domain.h"

#include <string.h>
#include <float.h>

#include "esp_log.h"


/* --- Default scheduler weights --- */
#define BASE_PRIORITY_WEIGHT        1.0f
#define URGENCY_WEIGHT              4.0f
#define AGE_WEIGHT                  0.2f
#define IGNORE_PENALTY_WEIGHT       1.0f
#define PREEMPT_DELTA               0.8f
#define MIN_DWELL_TIME_MS           20000  // 20 seconds

/* --- Weight caps --- */
#define URGENCY_CAP          10.0f
#define AGE_CAP              7.0f

/* --- Growth rate (smaller number = faster growth over time) --- */
#define URGENCY_GROWTH_RATE  1      // Grows by 1 every minute the task is overdue.
#define AGE_GROWTH_RATE      2      // Grows by 1 every 2 minutes since the task was created.

static const char *TAG = "trace_sched";


/* ----------------------------- Internals ----------------------------- */

static inline void scheduler_clear_active(scheduler *s, time_ms current_time_ms) {
    s->has_active_task = false;
    s->active_task_id.index = UINT16_MAX;
    s->active_task_id.generation = 0;
    s->task_active_since = current_time_ms;
}


static float calculate_task_score(const scheduler *scheduler_instance, const task *task_instance, time_ms current_time) {
    float base_priority = (float)task_instance->base_priority;

    float overdue_ms =
        (current_time > task_instance->time_limit) ?
        (float)(current_time - task_instance->time_limit) : 0.0f;

    float urgency = overdue_ms / (60000.0f * URGENCY_GROWTH_RATE);
    if (urgency > URGENCY_CAP) urgency = URGENCY_CAP;

    float age_ms =
        (current_time > task_instance->created_at) ?
        (float)(current_time - task_instance->created_at) : 0.0f;

    float age = age_ms / (60000.0f * AGE_GROWTH_RATE);
    if (age > AGE_CAP) age = AGE_CAP;

    float ignore_penalty = (float)task_instance->ignore_count;

    float score = scheduler_instance->cfg.base_priority_weight * base_priority +
                  scheduler_instance->cfg.urgency_weight * urgency +
                  scheduler_instance->cfg.age_weight * age -
                  scheduler_instance->cfg.ignore_penalty_weight * ignore_penalty;

    ESP_LOGD(TAG,
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
        
        ESP_LOGI(TAG, "block dwell elapsed=%lu min=%lu",
            (unsigned long)(current_time - scheduler->task_active_since),
            (unsigned long)effective_min_dwell_time);
   
        return false;
    }

    float effective_preempt_delta = recompute_preempt_delta(scheduler);
    
    if (!(candidate_task_score > active_task_score + effective_preempt_delta)) {

        ESP_LOGI(TAG, "block margin candidate=%.2f need>%.2f (a=%.2f + d=%.2f)",
            (double)candidate_task_score,
            (double)(active_task_score + effective_preempt_delta),
            (double)active_task_score,
            (double)effective_preempt_delta);

        return false;
    }
    
    ESP_LOGI(TAG, "allow: switch");
    return true;
}


/* ----------------------------- API ----------------------------- */

void scheduler_init(scheduler *scheduler, const scheduler_config *cfg) {
    memset(scheduler, 0, sizeof(*scheduler));
    if (cfg) scheduler->cfg = *cfg;

    // Defaults
    if (scheduler->cfg.base_priority_weight == 0)  scheduler->cfg.base_priority_weight = BASE_PRIORITY_WEIGHT;
    if (scheduler->cfg.urgency_weight == 0)        scheduler->cfg.urgency_weight = URGENCY_WEIGHT;
    if (scheduler->cfg.age_weight == 0)            scheduler->cfg.age_weight = AGE_WEIGHT;
    if (scheduler->cfg.ignore_penalty_weight == 0) scheduler->cfg.ignore_penalty_weight = IGNORE_PENALTY_WEIGHT;

    if (scheduler->cfg.preempt_delta == 0)         scheduler->cfg.preempt_delta = PREEMPT_DELTA;
    if (scheduler->cfg.min_dwell_time_ms == 0)     scheduler->cfg.min_dwell_time_ms = MIN_DWELL_TIME_MS;

    scheduler->human_state_indicator = 0.0f;
    scheduler->has_active_task = false;
    scheduler->task_active_since = 0;
    scheduler->active_task_id.index = UINT16_MAX;
    scheduler->active_task_id.generation = 0;
}


/* --- Scheduler tick recalculates score of each task --- */

void scheduler_tick(scheduler *scheduler_instance, task_pool *pool, time_ms current_time) {
    /* Variables used for logging */
    static task_id last_logged_active = { .index = UINT16_MAX, .generation = 0 };
    bool active_changed = false;
    /* -------------------------- */


    task_id best_task_id = { .index = UINT16_MAX, .generation = 0 };
    float best_task_score = -FLT_MAX;

    /* Scan pool for best schedulable task */
    for (uint16_t index = 0; index < TASK_POOL_CAPACITY; ++index) {
        task_slot *slot = &pool->slots[index];
        if (slot->occupied == false) continue;

        task *candidate_task = &slot->task_instance;

        /* keep tasks up-to-date */
        refresh_task(candidate_task, current_time);

        if (candidate_task->status != TASK_ELIGIBLE) continue;

        float candidate_score = calculate_task_score(scheduler_instance, candidate_task, current_time);

        if (candidate_score > best_task_score) {
            best_task_score = candidate_score;
            best_task_id.index = index;
            best_task_id.generation = slot->generation;
        }
    }

    if (best_task_id.index == UINT16_MAX) {
        if (scheduler_instance->has_active_task) {
            ESP_LOGI(TAG, "no_schedulable t=%lu -> clearing active", (unsigned long)current_time);
        }
        scheduler_clear_active(scheduler_instance, current_time);
        return;
    }

    if (!scheduler_instance->has_active_task) {
        scheduler_instance->has_active_task = true;
        scheduler_instance->active_task_id = best_task_id;
        scheduler_instance->task_active_since = current_time;
        active_changed = true;

        ESP_LOGI(TAG,
            "init_select t=%lu active=(%u,%u) score=%.2f",
            (unsigned long)current_time,
            (unsigned)best_task_id.index,
            (unsigned)best_task_id.generation,
            (double)best_task_score);
        goto done;
    }

    task *active_task = task_pool_get(pool, scheduler_instance->active_task_id);
    if (!active_task) {
        /* active handle stale: take best */
        ESP_LOGW(TAG,
            "active_stale t=%lu switching_to_best=(%u,%u)",
            (unsigned long)current_time,
            (unsigned)best_task_id.index, (unsigned)best_task_id.generation);
   
        scheduler_instance->active_task_id = best_task_id;
        scheduler_instance->task_active_since = current_time;
        active_changed = true;
        goto done;
    }

    refresh_task(active_task, current_time);

    if (active_task->status != TASK_ELIGIBLE) {
        scheduler_instance->active_task_id = best_task_id;
        scheduler_instance->task_active_since = current_time;
        active_changed = true;
        goto done;
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
        
    ESP_LOGD(TAG,
        "dec t=%lu active=(%u,%u) a=%.2f best=(%u,%u) b=%.2f dwell=%lu/%lu d=%.2f",
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

        if (scheduler_instance->active_task_id.index != last_logged_active.index ||
            scheduler_instance->active_task_id.generation != last_logged_active.generation) {
            ESP_LOGI(TAG,
                "SWITCH t=%lu -> active=(%u,%u)",
                (unsigned long)current_time,
                (unsigned)best_task_id.index,
                (unsigned)best_task_id.generation);

            last_logged_active = best_task_id;
        }
            
        scheduler_instance->active_task_id = best_task_id;
        scheduler_instance->task_active_since = current_time;
        active_changed = true;
    }


    done:
        // Log active task if task progression was triggered
        if (active_changed) {
            task *t = task_pool_get(pool, scheduler_instance->active_task_id);
            if (t) {
                ESP_LOGI(TAG,
                    "active_now=%s (table=%u) t=%lu (%u,%u)",
                    task_kind_to_str(t->kind),
                    t->table_number,
                    (unsigned long)current_time,
                    (unsigned)scheduler_instance->active_task_id.index,
                    (unsigned)scheduler_instance->active_task_id.generation);
            } else {
                ESP_LOGI(TAG,
                    "active_now=STALE t=%lu (%u,%u)",
                    (unsigned long)current_time,
                    (unsigned)scheduler_instance->active_task_id.index,
                    (unsigned)scheduler_instance->active_task_id.generation);
            }
        }
        return;
}

