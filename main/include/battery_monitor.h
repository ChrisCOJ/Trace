#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float voltage;
    uint8_t bars;        // 0..4
    bool initialized;
} battery_monitor_state;

void battery_monitor_init(void);
void battery_monitor_update(void);

float battery_monitor_get_voltage(void);
uint8_t battery_monitor_get_bars(void);
battery_monitor_state battery_monitor_get_state(void);

#ifdef __cplusplus
}
#endif

#endif