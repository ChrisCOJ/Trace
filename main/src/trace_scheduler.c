#include "../include/trace_scheduler.h"
#include "../include/task_domain.h"

#include <string.h>
#include <float.h>

#include "esp_log.h"


/* --- Base scoring weights and caps --- */
#define BASE_PRIORITY_WEIGHT        1.0f
#define URGENCY_WEIGHT              2.0f
#define AGE_WEIGHT                  0.4f
#define IGNORE_PENALTY_WEIGHT       2.0f
#define URGENCY_CAP                 10.0f
#define AGE_CAP                     7.0f
#define URGENCY_GROWTH_RATE         1.0f       // units per minute overdue
#define AGE_GROWTH_RATE             1.0f       // units per minute overdue

/* --- Switch prompt gate --- */
#define PREEMPT_DELTA               2.0f
#define MIN_DWELL_TIME_MS           15000

/* --- Challenger adjustment defaults --- */
#define ZONE_BATCH_BONUS            1.0f
#define CROSS_ZONE_PENALTY          1.0f


static const char *TAG = "trace_sched";


typedef struct {
    task_id best_id;
    float best_score;
    uint8_t pending_count;
    uint8_t critical_count;
    task_id top_critical_id;
    float top_critical_score;
} scheduler_scan_result;


/* ------------------------------------------------------------------ */
/* Zone model                                                          */
/* ------------------------------------------------------------------ */

typedef enum { ZONE_A, ZONE_B, ZONE_UNKNOWN } table_zone;

static table_zone zone_of_table(uint8_t table_number) {
    if ((table_number >= 1 && table_number <= 10) || (table_number >= 18 && table_number <= 24)) return ZONE_A;
    if (table_number >= 11 && table_number <= 17) return ZONE_B;
    return ZONE_UNKNOWN;
}


/* ------------------------------------------------------------------ */
/* Batch-compatibility matrix                                          */
/*   BATCH_COMPAT[active_kind][challenger_kind]                       */
/*   Scales zone_batch_bonus: 1.0=full, 0.5=partial, 0.25=marginal,  */
/*   0.0=incompatible (different area/equipment/workflow)             */
/* ------------------------------------------------------------------ */
/*                       SW      TO      PO      SO      MT      PB      CT  */
static const float BATCH_COMPAT[7][7] = {
    [SERVE_WATER]   = { 1.00f,  1.00f,  0.00f,  0.25f,  1.00f,  0.25f,  0.00f },
    [TAKE_ORDER]    = { 1.00f,  1.00f,  0.00f,  0.50f,  1.00f,  1.00f,  0.00f },
    [PREPARE_ORDER] = { 0.00f,  0.00f,  1.00f,  1.00f,  0.00f,  0.00f,  0.00f },
    [SERVE_ORDER]   = { 0.25f,  0.50f,  0.00f,  1.00f,  0.50f,  0.50f,  0.00f },
    [MONITOR_TABLE] = { 1.00f,  1.00f,  0.00f,  0.50f,  1.00f,  0.50f,  0.00f },
    [PRESENT_BILL]  = { 0.25f,  1.00f,  0.00f,  0.50f,  0.50f,  1.00f,  0.50f },
    [CLEAR_TABLE]   = { 0.00f,  0.25f,  0.00f,  0.00f,  0.00f,  0.50f,  1.00f },
};


/* ------------------------------------------------------------------ */
/* Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static inline void scheduler_clear_active(scheduler *s, time_ms current_time) {
    s->has_active_task = false;
    s->active_task_id.index      = UINT16_MAX;
    s->active_task_id.generation = 0;
    s->task_active_since = current_time;
}


static float compute_task_urgency(const task *t, time_ms current_time) {
    float overdue_ms = (current_time > t->time_limit)
        ? (float)(current_time - t->time_limit) : 0.0f;
    float urgency = overdue_ms / (60000.0f * URGENCY_GROWTH_RATE);
    return (urgency > URGENCY_CAP) ? URGENCY_CAP : urgency;
}


static float task_base_score(const scheduler *s, const task *t, time_ms current_time) {
    float urgency = compute_task_urgency(t, current_time);

    float age_ms = (current_time > t->created_at)
        ? (float)(current_time - t->created_at) : 0.0f;
    float age = age_ms / (60000.0f * AGE_GROWTH_RATE);
    if (age > AGE_CAP) age = AGE_CAP;

    float score = s->cfg.base_priority_weight * (float)t->base_priority
                + s->cfg.urgency_weight        * urgency
                + s->cfg.age_weight            * age
                - s->cfg.ignore_penalty_weight * (float)t->ignore_count;

    ESP_LOGD(TAG, "base id=%u bp=%.2f age=%.2f urg=%.2f score=%.2f",
        t->id.index, (double)t->base_priority, (double)age, (double)urgency, (double)score);

    return score;
}


/* Zone-batch adjustment applied to challenger tasks to influence
   next-task selection at natural breakpoints. */
static float challenger_zone_adjustment(const scheduler *s,
                                        const task *active_task,
                                        const task *challenger_task)
{
    if (!active_task) return 0.0f;

    float adjustment = 0.0f;
    table_zone active_zone     = zone_of_table(active_task->table_number);
    table_zone challenger_zone = zone_of_table(challenger_task->table_number);

    if (active_zone != ZONE_UNKNOWN && challenger_zone == active_zone) {
        int active_kind = (int)active_task->kind;
        int challenger_kind = (int)challenger_task->kind;
        if (active_kind < (int)TASK_NOT_APPLICABLE && challenger_kind < (int)TASK_NOT_APPLICABLE)
            adjustment += s->cfg.zone_batch_bonus * BATCH_COMPAT[active_kind][challenger_kind];
    }

    if (active_zone != ZONE_UNKNOWN && challenger_zone != ZONE_UNKNOWN && challenger_zone != active_zone)
        adjustment -= s->cfg.cross_zone_penalty;

    return adjustment;
}


static bool task_id_is_valid(task_id id) {
    return id.index != UINT16_MAX;
}


static bool active_task_is_usable(task *active_task) {
    return active_task && active_task->status == TASK_ELIGIBLE;
}


static void scheduler_set_active_task(scheduler *sched, task_id id, time_ms now) {
    sched->has_active_task = true;
    sched->active_task_id = id;
    sched->task_active_since = now;
}


static void scheduler_clear_critical_state(scheduler *sched) {
    sched->critical_count = 0;
    sched->top_critical_id = (task_id){ .index = UINT16_MAX, .generation = 0 };
}


static scheduler_scan_result scheduler_scan_tasks(scheduler *sched, task_pool *pool, task *active_task, float active_raw_priority, bool dwell_satisfied, time_ms current_time) {
    scheduler_scan_result result = {
            .best_id = { .index = UINT16_MAX, .generation = 0 },
            .best_score = -FLT_MAX,
            .pending_count = 0,
            .critical_count = 0,
            .top_critical_id = { .index = UINT16_MAX, .generation = 0 },
            .top_critical_score = -FLT_MAX,
        };

    for (uint16_t i = 0; i < TASK_POOL_CAPACITY; ++i) {
        task_slot *slot = &pool->slots[i];
        if (!slot->occupied) {
            continue;
        }

        task *task_inst = &slot->task_instance;
        refresh_task(task_inst, current_time);

        if (task_inst->status != TASK_ELIGIBLE) {
            continue;
        }

        bool is_active = false;
        if (sched->has_active_task && active_task) {
            is_active =
                (task_inst->id.index == sched->active_task_id.index) &&
                (task_inst->id.generation == sched->active_task_id.generation);
        }

        float raw_priority = task_base_score(sched, task_inst, current_time);

        /* Zone adjustment affects next-task ranking only.
           It must never influence the raw preemption threshold. */
        float ranking_score = is_active
            ? raw_priority
            : (raw_priority + challenger_zone_adjustment(sched, active_task, task_inst));

        if (ranking_score > result.best_score) {
            result.best_score = ranking_score;
            result.best_id = task_inst->id;
        }

        if (!is_active) {
            result.pending_count++;

            if (dwell_satisfied &&
                raw_priority > (active_raw_priority + sched->cfg.preempt_delta)) {

                result.critical_count++;

                if (ranking_score > result.top_critical_score) {
                    result.top_critical_score = ranking_score;
                    result.top_critical_id = task_inst->id;
                }
            }
        }
    }

    return result;
}


/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

void scheduler_init(scheduler *s, const scheduler_config *cfg) {
    memset(s, 0, sizeof(*s));
    if (cfg) s->cfg = *cfg;

    if (s->cfg.base_priority_weight == 0)  s->cfg.base_priority_weight   = BASE_PRIORITY_WEIGHT;
    if (s->cfg.urgency_weight == 0)        s->cfg.urgency_weight         = URGENCY_WEIGHT;
    if (s->cfg.age_weight == 0)            s->cfg.age_weight             = AGE_WEIGHT;
    if (s->cfg.ignore_penalty_weight == 0) s->cfg.ignore_penalty_weight  = IGNORE_PENALTY_WEIGHT;
    if (s->cfg.preempt_delta == 0)         s->cfg.preempt_delta          = PREEMPT_DELTA;
    if (s->cfg.min_dwell_time_ms == 0)     s->cfg.min_dwell_time_ms      = MIN_DWELL_TIME_MS;
    if (s->cfg.zone_batch_bonus == 0)      s->cfg.zone_batch_bonus       = ZONE_BATCH_BONUS;
    if (s->cfg.cross_zone_penalty == 0)    s->cfg.cross_zone_penalty     = CROSS_ZONE_PENALTY;

    s->has_active_task           = false;
    s->active_task_id.index      = UINT16_MAX;
    s->active_task_id.generation = 0;
    s->task_active_since         = 0;
    s->pending_count             = 0;
    s->critical_count            = 0;
    s->top_critical_id.index     = UINT16_MAX;
    s->top_critical_id.generation = 0;
}


void scheduler_tick(scheduler *sched, task_pool *pool, time_ms current_time) {
    task *active_task = NULL;
    float active_raw_priority = -FLT_MAX;
    bool active_usable = false;
    bool active_task_changed = false;

    if (sched->has_active_task) {
        active_task = task_pool_get(pool, sched->active_task_id);
        if (active_task) {
            refresh_task(active_task, current_time);
            active_usable = (active_task->status == TASK_ELIGIBLE);
            if (active_usable) {
                active_raw_priority = task_base_score(sched, active_task, current_time);
            }
        }
    }

    time_ms dwell_elapsed = current_time - sched->task_active_since;
    bool dwell_satisfied = (dwell_elapsed >= sched->cfg.min_dwell_time_ms);

    scheduler_scan_result scan = scheduler_scan_tasks(sched, pool, active_task, active_raw_priority, dwell_satisfied, current_time);

    sched->pending_count   = scan.pending_count;
    sched->critical_count  = scan.critical_count;
    sched->top_critical_id = scan.top_critical_id;

    // Case 1: current active task is still valid. Keep it
    if (sched->has_active_task && active_usable) {
        return;
    }

    // Case 2: no valid active task remains, but a replacement exists
    if (scan.best_id.index != UINT16_MAX) {
        bool was_uninitialised = !sched->has_active_task;
        bool was_stale = sched->has_active_task && (active_task == NULL);
        bool was_ineligible = sched->has_active_task && active_task && !active_usable;

        sched->has_active_task   = true;
        sched->active_task_id    = scan.best_id;
        sched->task_active_since = current_time;
        active_task_changed      = true;

        /* Natural transition onto a new task should not leave behind a stale
           critical-switch prompt for the task we are auto-selecting */
        sched->critical_count  = 0;
        sched->top_critical_id = (task_id){ .index = UINT16_MAX, .generation = 0 };

        if (was_uninitialised) {
            ESP_LOGI(TAG, "init_select t=%lu active=(%u,%u) score=%.2f",
                (unsigned long)current_time,
                (unsigned)scan.best_id.index,
                (unsigned)scan.best_id.generation,
                (double)scan.best_score);
        } else if (was_stale) {
            ESP_LOGW(TAG, "active_stale t=%lu -> best=(%u,%u) score=%.2f",
                (unsigned long)current_time,
                (unsigned)scan.best_id.index,
                (unsigned)scan.best_id.generation,
                (double)scan.best_score);
        } else if (was_ineligible) {
            ESP_LOGI(TAG, "active_ineligible t=%lu -> best=(%u,%u) score=%.2f",
                (unsigned long)current_time,
                (unsigned)scan.best_id.index,
                (unsigned)scan.best_id.generation,
                (double)scan.best_score);
        }
    }
    // Case 3: no valid active task remains, and no eligible replacement exists
    else {
        ESP_LOGI(TAG, "no_schedulable t=%lu -> clearing active", (unsigned long)current_time);
        scheduler_clear_active(sched, current_time);
        sched->pending_count   = 0;
        sched->critical_count  = 0;
        sched->top_critical_id = (task_id){ .index = UINT16_MAX, .generation = 0 };
        return;
    }

    if (active_task_changed) {
        task *newly_active = task_pool_get(pool, sched->active_task_id);
        if (!newly_active) {
            ESP_LOGE(TAG, "scheduler_tick done: Could not find the task id in the task pool.");
            scheduler_clear_active(sched, current_time);
            return;
        }

        ESP_LOGI(TAG, "active_now=%s (table=%u) t=%lu (%u,%u)",
            task_kind_to_str(newly_active->kind),
            newly_active->table_number,
            (unsigned long)current_time,
            (unsigned)sched->active_task_id.index,
            (unsigned)sched->active_task_id.generation);
    }
}


void scheduler_force_active(scheduler *s, task_id id, time_ms current_time_ms) {
    s->has_active_task   = true;
    s->active_task_id    = id;
    s->task_active_since = current_time_ms;
    ESP_LOGI(TAG, "force_active (%u,%u) t=%lu",
        (unsigned)id.index, (unsigned)id.generation, (unsigned long)current_time_ms);
}
