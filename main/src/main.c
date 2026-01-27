#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../include/mpu_i2c.h"
#include "../include/display_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "../include/trace_scheduler.h"
#include "../include/task_pool.h"
#include "../include/task_domain.h"



static void seed_dummy_tasks(task_pool *pool, time_ms now)
{
    task_id id1 = task_pool_allocate(pool);
    task_id id2 = task_pool_allocate(pool);
    task_id id3 = task_pool_allocate(pool);

    task *t1 = task_pool_get(pool, id1);
    task *t2 = task_pool_get(pool, id2);
    task *t3 = task_pool_get(pool, id3);

    /* Task 1: overdue, medium base priority */
    task_init(t1, id1,
              3.0f,
              now - 180000,     /* created 3 min ago */
              now - 60000,      /* due 1 min ago → overdue */
              12);

    /* Task 2: not due yet, higher base, already ignored once */
    task_init(t2, id2,
              6.0f,
              now - 30000,      /* created 30s ago */
              now + 180000,     /* due in 3 min */
              7);
    t2->ignore_count = 1;

    /* Task 3: suppressed (should not be schedulable yet) */
    task_init(t3, id3,
              10.0f,
              now - 600000,     /* created 10 min ago */
              now - 300000,     /* due 5 min ago */
              3);
    t3->status = TASK_SUPPRESSED;
    t3->suppress_until = now + 45000; /* wake up in 45s */
}


static void scheduler_test_task(void *arg) {
    task_pool pool;
    scheduler scheduler_instance;
    bool ignored_once = false;

    task_pool_init(&pool);

    scheduler_config config = {
        .base_priority_weight = 1.0f,
        .urgency_weight = 4.0f,
        .age_weight = 0.2f,
        .ignore_penalty_weight = 1.0f,
        .preempt_delta = 0.8f,
        .min_dwell_time_ms = 20000,
        .extra_dwell_ms_at_max_exhaustion = 0,
        .extra_delta_at_max_exhaustion = 0.0f,
    };

    scheduler_init(&scheduler_instance, &config);

    time_ms now = get_time() + 600000;
    seed_dummy_tasks(&pool, now);

    while (1) {
        now = get_time() + 600000;

        // Simulate user ignore task
        if (now >= 670000 && !ignored_once) {
            scheduler_handle_action(&scheduler_instance, &pool, USER_ACTION_IGNORE, now);
            ignored_once = true;
        }

        scheduler_tick(&scheduler_instance, &pool, now);
        
        vTaskDelay(pdMS_TO_TICKS(1000));

    }
}



void app_main(void)
{
    esp_log_level_set("trace_sched", ESP_LOG_INFO);
    esp_log_level_set("task_domain", ESP_LOG_INFO);

    xTaskCreate(scheduler_test_task, "sched_test", 4096, NULL, 5, NULL);


    // /* ----------------------------- Init mpu6050 ----------------------------- */
    // mpu6050_i2c_context ctx = setup_mpu6050_i2c();
    // i2c_master_dev_handle_t mpu6050_dev_handle = ctx.dev_handle;

    // esp_err_t mpu_init_status = mpu_init(mpu6050_dev_handle, MPU6050_ACCEL_4G, MPU6050_GYRO_500_DEG);
    // if (mpu_init_status) return;
    /* ------------------------------------------------------------------------ */

    /* ----------------------------- Init display ----------------------------- */

    // display_spi_ctx display_ctx = display_init();
    // if (display_ctx.ret_code != 0) return;
    // spi_device_handle_t display_handle = display_ctx.dev_handle;

    // // Test fill screen with white pixels 
    // display_fill_white(display_handle);
    /* ------------------------------------------------------------------------ */
}