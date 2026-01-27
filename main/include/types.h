#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_timer.h"


typedef uint32_t time_ms;

static inline time_ms get_time() {
    return (time_ms)(esp_timer_get_time() / 1000ULL);
}

static inline time_ms get_time_elapsed(time_ms start_of_task) {
    return get_time() - start_of_task;
}



#endif