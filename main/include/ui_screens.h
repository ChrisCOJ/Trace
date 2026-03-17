#ifndef UI_SCREENS_H
#define UI_SCREENS_H


#include "driver/spi_master.h"
#include "ui_internal.h"
#include <stdint.h>




void ui_draw_main(spi_device_handle_t display, ui_snapshot snapshot, ui_task_state state);


rect table_tile_rect(uint8_t index);


void ui_draw_grid(spi_device_handle_t display);


void draw_active_table_page(spi_device_handle_t display_handle, uint8_t table_index);





#endif