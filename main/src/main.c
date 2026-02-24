#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

#include "../include/display_util.h"
#include "../include/trace_system.h"
#include "../include/table_fsm.h"
#include "../include/trace_scheduler.h"
#include "../include/user_interface.h"
#include "../include/touch_controller_util.h"


void scheduler_tick_task(void *arg) {
    (void)arg;

    while (1) {
        time_ms current_time_ms = get_time();
        trace_system_tick(current_time_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


void app_main(void) {
    esp_log_level_set("trace_sched", ESP_LOG_INFO);
    esp_log_level_set("task_domain", ESP_LOG_INFO);
    esp_log_level_set("ui", ESP_LOG_INFO);
    esp_log_level_set("touch", ESP_LOG_WARN);

    /* Core scheduler setup */
    scheduler_config system_config = {0};
    trace_system_init(&system_config);

    /* Display and UI */
    display_spi_ctx display_context = display_init();
    ui_draw_layout(display_context.dev_handle);

    // /* Seed test table */
    // time_ms current_time = get_time();
    // system_apply_table_fsm_event(0, EVENT_CUSTOMERS_SEATED, current_time);

    /* Runtime tasks */
    xTaskCreate(ui_task, "ui_task", 4096, &display_context, 5, NULL);
    xTaskCreate(scheduler_tick_task, "sched_tick", 4096, NULL, 5, NULL);
}
