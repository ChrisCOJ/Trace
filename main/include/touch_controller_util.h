#ifndef TOUCH_CONTROLLER_UTIL_H
#define TOUCH_CONTROLLER_UTIL_H


#include <stdbool.h>
#include <stdint.h>



/**
 * Initialise the CST816S touch controller and its I2C/GPIO interface.
 *
 * Configures the touch controller GPIOs and installs the I2C driver, then
 * performs a hardware reset to ensure a known controller state.
 *
 * This function is safe to call multiple times; subsequent calls
 * return immediately once initialisation has completed.
 *
 * Timing / blocking behaviour:
 *  - Blocks during initialisation due to the reset delays (~60 ms total).
 *  - Performs no additional sleeping once already initialised.
 *
 * Call this during startup to avoid the first touch read incurring
 * the initialisation delay.
 */
void touch_init(void);


/**
 * Read the current touch position from the CST816S controller.
 *
 * Queries the touch controller over I2C and returns a single touch coordinate
 * when at least one finger is detected. Coordinates are returned in screen-space
 * pixels. If no finger is present, returns false and does not modify outputs.
 *
 * This function ensures the controller is initialised by calling touch_init()
 * internally. If initialisation has not yet occurred, the first call may block
 * for the duration of touch_init() (see touch_init() timing notes).
 *
 * Timing / blocking behaviour:
 *  - Performs an I2C transaction each call and may block waiting for I2C completion.
 *  - Does not sleep or delay explicitly (aside from any first-call initialisation).
 *
 * @param out_x Output pointer for X coordinate (pixels).
 * @param out_y Output pointer for Y coordinate (pixels).
 * @return true if a touch is present and coordinates were written; false otherwise.
 */
bool read_touch_point(uint16_t *out_x, uint16_t *out_y);



#endif 