#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

#include "driver/spi_master.h"
#include "../include/task_domain.h"
#include "../include/table_fsm.h"


/**
 * FreeRTOS task that polls the touch controller and dispatches UI actions.
 *
 * Periodically reads touch input, performs edge detection to avoid repeat
 * triggers, maps touch coordinates to UI actions, and forwards the resulting
 * actions to the scheduling and table FSM subsystems.
 *
 * This task runs indefinitely, includes a fixed delay between iterations,
 * and performs no blocking operations other than the RTOS delay.
 *
 * @param arg Unused task parameter.
 */
void ui_task(void *arg);


#endif