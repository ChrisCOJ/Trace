#ifndef HAPTIC_DRIVER_H
#define HAPTIC_DRIVER_H

#include <stdio.h>
#include "esp_err.h"


esp_err_t drv2605l_init(void);
esp_err_t drv2605l_play_effect(uint8_t effect_id);


#endif