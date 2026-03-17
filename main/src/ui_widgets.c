#include "../include/ui_widgets.h"
#include "../include/ui_internal.h"
#include "../include/font5x7.h"
#include "../include/table_fsm.h"
#include "../include/trace_system.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "driver/spi_master.h"



void draw_label(spi_device_handle_t display, rect r, const char *label, size_t label_len, uint16_t text_color, bool snap_left) {
    // Split label in parts and arrange vertically if too long for its container.
    if (label_len * CHAR_WIDTH * UI_TEXT_SCALE > r.w) {
        char label_cpy[32];
        snprintf(label_cpy, sizeof(label_cpy), "%s", label);
        char *space = strchr(label_cpy, ' ');
        if (space) {
            const uint8_t VERTICAL_SPACING = 7;
            // Split the text vertically after the first space
            *space = '\0';
            const char *first_part = label_cpy;
            const char *second_part = space + 1;

            uint16_t first_part_y = r.y + r.h/2 - (CHAR_HEIGHT * UI_TEXT_SCALE + VERTICAL_SPACING);
            uint16_t second_part_y = first_part_y + CHAR_HEIGHT * UI_TEXT_SCALE + VERTICAL_SPACING;
            uint16_t first_part_x = snap_left ? r.x : r.x + r.w/2 - strlen(first_part) * CHAR_WIDTH * UI_TEXT_SCALE/2;
            uint16_t second_part_x = snap_left ? r.x : r.x + r.w/2 - strlen(second_part) * CHAR_WIDTH * UI_TEXT_SCALE/2;

            draw_text(display, first_part_x, first_part_y, first_part, text_color, UI_TEXT_SCALE);
            draw_text(display, second_part_x, second_part_y, second_part, text_color, UI_TEXT_SCALE);
            return;
        }
        else {
            // No space to split — fall back to scale 1, truncating if still too long
            size_t max_chars = r.w / (CHAR_WIDTH * UI_SMALL_TEXT_SCALE);
            size_t draw_len = (label_len < max_chars) ? label_len : max_chars;
            uint16_t text_y = r.y + r.h/2 - CHAR_HEIGHT * UI_SMALL_TEXT_SCALE/2;
            uint16_t text_x = snap_left ? r.x : r.x + r.w/2 - draw_len * CHAR_WIDTH * UI_SMALL_TEXT_SCALE/2;
            char truncated[32];
            snprintf(truncated, sizeof(truncated), "%.*s", (int)draw_len, label);
            draw_text(display, text_x, text_y, truncated, text_color, UI_SMALL_TEXT_SCALE);
            return;
        }
    }

    uint16_t text_y = r.y + r.h/2 - CHAR_HEIGHT * UI_TEXT_SCALE/2;
    uint16_t text_x = snap_left ? r.x : r.x + r.w/2 - label_len * CHAR_WIDTH * UI_TEXT_SCALE/2;

    draw_text(display, text_x, text_y, label, text_color, UI_TEXT_SCALE);
}


/* Draw a filled rectangle using line-by-line writes. Includes option to round corners */
void draw_filled_rect(spi_device_handle_t display,
    uint16_t x, uint16_t y,
    uint16_t width, uint16_t height,
    uint16_t color_rgb565,
    uint8_t radius) {
    /* Scanline buffer sized for max UI element width */
    static uint16_t scanline_buffer[DISPLAY_WIDTH];
    
    if (width > DISPLAY_WIDTH || height > DISPLAY_HEIGHT) return;

    uint16_t max_radius = (width < height ? width : height) / 2;
    if (radius > max_radius) {
        radius = max_radius;
    }

    /* Build x offset buffer */
    static uint8_t x_offset_buf[DISPLAY_HEIGHT];
    for (int i = 0; i <= radius; ++i) {
        uint8_t dy = radius - i;
        uint8_t dx = floor(sqrt(radius * radius - dy * dy));
        uint8_t x_offset = radius - dx;
        x_offset_buf[i] = x_offset;
    }

    // x offset for non-corners is 0
    for (int i = (radius + 1); i < (height - radius); ++i) {
        x_offset_buf[i] = 0;
    }
    
    for (int i = radius; i >= 0; --i) {
        uint8_t dy = radius - i;
        uint8_t dx = floor(sqrt(radius * radius - dy * dy));
        uint8_t x_offset = radius - dx;
        x_offset_buf[height - i - 1] = x_offset;
    }

    for (int row = 0; row < height; ++row) {
        uint8_t x_off   = x_offset_buf[row];
        uint16_t seg_w  = width - 2 * x_off;

        for (int i = 0; i < seg_w; ++i) {
            scanline_buffer[i] = color_rgb565;
        }
        /* Write only the visible segment — corner pixels are never touched,
           so a bordered button's outer pixels are preserved. */
        display_write(display, x + x_off, (uint16_t)(y + row), seg_w, 1, scanline_buffer);
    }
}


/* Draw a bordered action button.  A 2-px border is drawn in the button's
   accent colour; the interior is filled, then the label is centred. */
void draw_button(spi_device_handle_t display, rect r, const char *label, btn_style style) {
    const uint8_t BORDER_W = 2;

    uint16_t fill, border, text;
    switch (style) {
        case BTN_PRIMARY:
            fill   = PRIMARY_ACCENT_COLOR;
            border = BUTTON_BORDER;
            text   = COLOR_LABEL_PRIMARY;
            break;
        case BTN_SECONDARY:
            fill   = SECONDARY_ACCENT_COLOR;
            border = BUTTON_BORDER;
            text   = COLOR_LABEL_SECONDARY;
            break;
        case BTN_DANGER:
            fill   = SECONDARY_ACCENT_COLOR;
            border = DANGER_COLOR;
            text   = DANGER_COLOR;
            break;
        default: /* BTN_DISABLED */
            fill   = DARK_GREY;
            border = GREY;
            text   = GREY;
            break;
    }

    /* Outer border rect */
    draw_filled_rect(display, r.x, r.y, r.w, r.h, border, UI_CORNER_RADIUS);
    /* Inner fill rect (inset by BORDER_W on every side) */
    draw_filled_rect(display,
        r.x + BORDER_W, r.y + BORDER_W,
        r.w - 2 * BORDER_W, r.h - 2 * BORDER_W,
        fill, UI_CORNER_RADIUS - BORDER_W);
    draw_label(display, r, label, strlen(label), text, false);
}


/* ---------------- Button wrappers ---------------- */
void draw_button_complete(spi_device_handle_t display) {
    draw_button(display, MAIN_COMPLETE_BTN, "Complete", BTN_PRIMARY);
}

void draw_button_start(spi_device_handle_t display) {
    draw_button(display, MAIN_START_BTN, "Start", BTN_PRIMARY);
}

void draw_button_bill(spi_device_handle_t display) {
    draw_button(display, MAIN_BILL_BTN, "Bill", BTN_SECONDARY);
}

void draw_button_ignore(spi_device_handle_t display) {
    draw_button(display, MAIN_IGNORE_BTN, "Ignore", BTN_SECONDARY);
}

void draw_button_take_order(spi_device_handle_t display) {
    draw_button(display, MAIN_TAKEORDER_BTN, "Take Order", BTN_SECONDARY);
}


/* Draw the pressed (style-inverted) version of a named button. */
void draw_button_highlight(spi_device_handle_t display, ui_action act) {
    switch (act) {
        case UI_ACTION_START_TASK:
            draw_button(display, MAIN_START_BTN,              "Start",      BTN_SECONDARY); break;
        case UI_ACTION_COMPLETE:
            draw_button(display, MAIN_COMPLETE_BTN,           "Complete",   BTN_SECONDARY); break;
        case UI_ACTION_IGNORE:
            draw_button(display, MAIN_IGNORE_BTN,             "Ignore",     BTN_PRIMARY);   break;
        case UI_ACTION_BILL:
            draw_button(display, MAIN_BILL_BTN,               "Bill",       BTN_PRIMARY);   break;
        case UI_ACTION_TAKE_ORDER:
            draw_button(display, MAIN_TAKEORDER_BTN,          "Take Order", BTN_PRIMARY);   break;
        case UI_ACTION_TABLE_INFO_TAKE_ORDER:
            draw_button(display, TABLE_INFO_TAKE_ORDER_BTN,   "Take Order", BTN_SECONDARY); break;
        case UI_ACTION_TABLE_INFO_BILL:
            draw_button(display, TABLE_INFO_BILL_BTN,         "Bill",       BTN_PRIMARY);   break;
        case UI_ACTION_TABLE_INFO_UNDO:
            draw_button(display, TABLE_INFO_UNDO_BTN,         "Undo",       BTN_PRIMARY);   break;
        default: break;
    }
}


/* Restore a button to its normal style after a press animation. */
void restore_button(spi_device_handle_t display, ui_action act, uint8_t sel_table) {
    switch (act) {
        case UI_ACTION_START_TASK:
            draw_button(display, MAIN_START_BTN,     "Start",      BTN_PRIMARY);   break;
        case UI_ACTION_COMPLETE:
            draw_button(display, MAIN_COMPLETE_BTN,  "Complete",   BTN_PRIMARY);   break;
        case UI_ACTION_IGNORE:
            draw_button(display, MAIN_IGNORE_BTN,    "Ignore",     BTN_SECONDARY); break;
        case UI_ACTION_BILL:
            draw_button(display, MAIN_BILL_BTN,      "Bill",       BTN_SECONDARY); break;
        case UI_ACTION_TAKE_ORDER:
            draw_button(display, MAIN_TAKEORDER_BTN, "Take Order", BTN_SECONDARY); break;
        case UI_ACTION_TABLE_INFO_TAKE_ORDER: {
            table_state state = system_get_table_state(sel_table);
            draw_button(display, TABLE_INFO_TAKE_ORDER_BTN, "Take Order",
                        state_can_take_order(state) ? BTN_PRIMARY : BTN_DISABLED);
            break;
        }
        case UI_ACTION_TABLE_INFO_BILL: {
            table_state state = system_get_table_state(sel_table);
            draw_button(display, TABLE_INFO_BILL_BTN, "Bill",
                        state_can_request_bill(state) ? BTN_SECONDARY : BTN_DISABLED);
            break;
        }
        case UI_ACTION_TABLE_INFO_UNDO:
            draw_button(display, TABLE_INFO_UNDO_BTN, "Undo",
                        table_can_undo(system_get_table(sel_table)) ? BTN_SECONDARY : BTN_DISABLED);
            break;
        default: break;
    }
}


/* ------------------- Icons ------------------- */
/* Draw a coloured '!' to the left of where draw_label would centre label_len chars in rect r. */
#define URGENCY_ICON_SCALE  3
void draw_urgency_icon(spi_device_handle_t display, rect r, size_t label_len, uint16_t color) {
    int16_t label_x = (int16_t)(r.x + r.w / 2) - (int16_t)(label_len * CHAR_WIDTH * UI_TEXT_SCALE / 2);
    uint16_t text_y = r.y + r.h / 2 - CHAR_HEIGHT * URGENCY_ICON_SCALE / 2;
    int16_t icon_x  = label_x - CHAR_WIDTH * URGENCY_ICON_SCALE - 4;
    if (icon_x >= 0) {
        draw_text(display, (uint16_t)icon_x, text_y, "!", color, URGENCY_ICON_SCALE);
    }
}


void draw_battery_icon(spi_device_handle_t display, uint8_t bars) {
    // Body outline
    draw_filled_rect(display, UI_BATT_X, UI_BATT_Y, UI_BATT_W, UI_BATT_H, WHITE, 3);
    // Tip (centred vertically on the body)
    uint16_t tip_y = UI_BATT_Y + (UI_BATT_H - UI_BATT_TIP_H) / 2;
    draw_filled_rect(display, UI_BATT_X + UI_BATT_W, tip_y, UI_BATT_TIP_W, UI_BATT_TIP_H, WHITE, 0);
    // Clear interior
    draw_filled_rect(display,
        UI_BATT_X + UI_BATT_BORDER, UI_BATT_Y + UI_BATT_BORDER,
        UI_BATT_W - 2 * UI_BATT_BORDER, UI_BATT_H - 2 * UI_BATT_BORDER, BLACK, 1);

    if (bars == 0) return;

    // Bar area: 1px padding inside the cleared interior
    // 4 bars × 6px + 3 gaps × 2px = 30px total
    uint16_t bar_x0 = UI_BATT_X + UI_BATT_BORDER + 1;
    uint16_t bar_y  = UI_BATT_Y + UI_BATT_BORDER + 1;
    uint16_t bar_h  = UI_BATT_H - 2 * UI_BATT_BORDER - 2;
    uint16_t bar_stride  = (UI_BATT_W - UI_BATT_BORDER * 2) / UI_BATT_BARS;
    uint16_t bar_w = bar_stride - 2;  // bar_stride - 2px gap

    // uint16_t bar_color = (bars >= 3) ? GREEN : (bars == 2) ? YELLOW : RED;
    uint16_t bar_color = (bars <= 1) ? RED : WHITE;
    for (uint8_t i = 0; i < bars && i < UI_BATT_BARS; i++) {
        draw_filled_rect(display, bar_x0 + i * bar_stride, bar_y, bar_stride, bar_h, bar_color, 0);
    }
}


void draw_back_icon(spi_device_handle_t display) {
    const uint16_t icon_x = 20;
    const uint16_t icon_y = (UI_TOPBAR_H - CHAR_HEIGHT * UI_TEXT_SCALE) / 2;
    draw_text(display, icon_x, icon_y, "<", WHITE, UI_TEXT_SCALE);
}