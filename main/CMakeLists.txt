idf_component_register(SRCS "src/main.c" "src/display_util.c" "src/mpu_i2c.c"
                            "src/task_domain.c" "src/task_pool.c" "src/trace_scheduler.c" 
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_timer)