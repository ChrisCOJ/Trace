#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "driver/spi_master.h"
#include "../include/ui_internal.h"


void draw_label(spi_device_handle_t display, rect r, const char *label, size_t label_len, uint16_t text_color, bool snap_left);


void draw_filled_rect(spi_device_handle_t display,
    uint16_t x, uint16_t y,
    uint16_t width, uint16_t height,
    uint16_t color_rgb565,
    uint8_t radius);


void draw_button(spi_device_handle_t display, rect r, const char *label, btn_style style);


void draw_button_complete(spi_device_handle_t display);

void draw_button_start(spi_device_handle_t display);

void draw_button_bill(spi_device_handle_t display);

void draw_button_ignore(spi_device_handle_t display);

void draw_button_take_order(spi_device_handle_t display);


void draw_button_highlight(spi_device_handle_t display, ui_action act);


void draw_urgency_icon(spi_device_handle_t display, rect r, size_t label_len, uint16_t color);


void draw_battery_icon(spi_device_handle_t display, uint8_t bars);


void draw_back_icon(spi_device_handle_t display);


void restore_button(spi_device_handle_t display, ui_action act, uint8_t sel_table);


void draw_pending_badge(spi_device_handle_t display, uint8_t pending_count, uint8_t critical_count);


#endif