#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

#include "driver/spi_master.h"
#include "../include/task_domain.h"
#include "../include/table_fsm.h"


#define MAX_TABLES      28


typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_IGNORE,
    UI_ACTION_COMPLETE,
    UI_ACTION_START_TASK,
    UI_ACTION_TAKE_ORDER,
    UI_ACTION_CLOSE_TABLE,
    UI_ACTION_CLOSE_IGNORE,     // Ignore and close table button have the same coordinates and size
    UI_ACTION_OPEN_TABLES,
    UI_ACTION_BACK,
    UI_ACTION_TABLE_TILE_0,
    UI_ACTION_TABLE_TILE_1,
    UI_ACTION_TABLE_TILE_2,
    UI_ACTION_TABLE_TILE_3,
    UI_ACTION_TABLE_TILE_4,
    UI_ACTION_TABLE_TILE_5
} ui_action;


/* UI mode: main notifications vs table grid overlay vs take order table list */
typedef enum { 
    UI_MODE_MAIN, 
    UI_MODE_TABLE_GRID,
    UI_MODE_TAKE_ORDER_GRID,
} ui_mode;


typedef struct {
    bool has_task;
    task_id task_id;
    task_kind task_kind;
    uint8_t table_number;
} ui_snapshot;


extern table_context table_fsm_instances[MAX_TABLES];


/**
 * Render the UI layout to the display.
 *
 * Clears the display and draws the fixed button regions used for manual
 * interaction testing. This function performs synchronous SPI writes to
 * the display and blocks until all drawing operations have completed.
 *
 * Intended for use during initialisation or controlled UI refreshes,
 * not for high-frequency redraws.
 *
 * @param display SPI device handle for the target display.
 */
void ui_draw_layout(spi_device_handle_t display);


// /**
//  * Map a raw touch coordinate to a high-level UI action.
//  *
//  * Performs hit-testing against the defined button regions and returns
//  * the corresponding logical action. If the touch does not intersect any
//  * button region, UI_ACTION_NONE is returned.
//  *
//  * This function is pure and non-blocking.
//  *
//  * @param x Touch X coordinate in screen space.
//  * @param y Touch Y coordinate in screen space.
//  * @return Corresponding UI action, or UI_ACTION_NONE if no hit occurs.
//  */
// ui_action return_touch_action(uint16_t x, uint16_t y);


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