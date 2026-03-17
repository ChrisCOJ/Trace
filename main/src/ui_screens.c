#include "../include/ui_screens.h"
#include "../include/ui_internal.h"
#include "../include/ui_widgets.h"
#include "../include/font5x7.h"
#include "../include/task_domain.h"
#include "../include/table_fsm.h"
#include "../include/battery_monitor.h"
#include "../include/trace_system.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>



uint8_t UI_GRID_PAGE = 0;


/* ------------------- Internals ------------------- */
static void draw_bottom_button_layout(spi_device_handle_t display_handle, bool monitor, bool has_task) {
    // Clear previous buttons in the draw area
    rect bg_clear_rect = { .x = 0,  .y = 215,  .w = 240, .h = 60 };
    draw_filled_rect(display_handle, bg_clear_rect.x, bg_clear_rect.y, bg_clear_rect.w, bg_clear_rect.h, BLACK, 0);

    if (monitor) {
        draw_button_bill(display_handle);
        draw_button_take_order(display_handle);
    }
    else {
        draw_button(display_handle, MAIN_IGNORE_BTN, "Ignore", has_task ? BTN_SECONDARY : BTN_DISABLED);
    }
}


/* Returns DARK_GREY if no active task. */
static uint16_t task_kind_tile_color(task_kind kind) {
    switch (kind) {
        case SERVE_WATER:    return YELLOW;
        case TAKE_ORDER:     return YELLOW;
        case SERVE_ORDER:    return YELLOW;
        case PRESENT_BILL:   return YELLOW;
        case PREPARE_ORDER:  return RED;
        case MONITOR_TABLE:  return GREEN;
        case CLEAR_TABLE:    return GREEN;
        default:             return DARK_GREY;
    }
}


static void draw_active_task_label(spi_device_handle_t display, ui_snapshot snap) {
    rect task_label_rect = {.x=0, .y=50, .w=240, .h=70};
    draw_filled_rect(display, task_label_rect.x, task_label_rect.y, task_label_rect.w, task_label_rect.h, BG, 0);

    if (snap.has_task) {
        const char *task_kind_label = task_kind_to_str(snap.task_kind);
        rect task_kind_rect = {.x=0,.y=60,.w=240,.h=30};
        draw_urgency_icon(display, task_kind_rect, strlen(task_kind_label), task_kind_tile_color(snap.task_kind));
        draw_label(display, task_kind_rect, task_kind_label, strlen(task_kind_label), COLOR_LABEL_CHROME, false);

        char task_table_label[10];
        snprintf(task_table_label, sizeof(task_table_label), "Table %d", snap.table_number + 1);
        draw_label(display, (rect){.x=0,.y=70+CHAR_HEIGHT*UI_TEXT_SCALE,.w=240,.h=30}, task_table_label, strlen(task_table_label), COLOR_LABEL_CHROME, false);
    }
    else {
        const char *task_label = "NONE";
        draw_label(display, (rect){.x=0,.y=70,.w=240,.h=30}, task_label, strlen(task_label), COLOR_LABEL_CHROME, false);
    }
}


static const char *table_state_to_str(table_state state) {
    switch (state) {
        case TABLE_SEATED:            return "Seated";
        case TABLE_READY_FOR_ORDER:   return "Ready to Order";
        case TABLE_PLACED_ORDER:      return "Placed Order";
        case TABLE_WAITING_FOR_ORDER: return "Waiting";
        case TABLE_DINING:            return "Dining";
        case TABLE_CHECKUP:           return "Check-up";
        case TABLE_REQUESTED_BILL:    return "Bill Requested";
        case TABLE_DONE:              return "Done";
        default:                      return "Unknown";
    }
}

static void format_elapsed(time_ms elapsed_ms, char *buf, size_t len) {
    uint32_t total_s = elapsed_ms / 1000;
    uint32_t m = total_s / 60;
    uint32_t s = total_s % 60;
    if (m > 0) {
        snprintf(buf, len, "%um %02us", (unsigned)m, (unsigned)s);
    } else {
        snprintf(buf, len, "%us", (unsigned)s);
    }
}


/* ------------------- API ------------------- */
void ui_draw_main(spi_device_handle_t display, ui_snapshot snapshot, ui_task_state state) {
    const uint16_t COLOR_TOPBAR = GREY;
    const char *tables_label = "Tables";

    display_fill(display, BG);

    // top bar (chrome — white on grey)
    draw_filled_rect(display, MAIN_TABLES_BTN.x, MAIN_TABLES_BTN.y, MAIN_TABLES_BTN.w, MAIN_TABLES_BTN.h, COLOR_TOPBAR, 10);
    draw_label(display, MAIN_TABLES_BTN, tables_label, strlen(tables_label), COLOR_LABEL_CHROME, false);

    if (snapshot.has_task) {
        const char *task_kind_label = task_kind_to_str(snapshot.task_kind);
        rect task_kind_rect = {.x=0,.y=60,.w=240,.h=30};
        draw_urgency_icon(display, task_kind_rect, strlen(task_kind_label), task_kind_tile_color(snapshot.task_kind));
        draw_label(display, task_kind_rect, task_kind_label, strlen(task_kind_label), COLOR_LABEL_CHROME, false);

        char task_table_label[10];
        snprintf(task_table_label, sizeof(task_table_label), "Table %d", snapshot.table_number + 1);
        draw_label(display, (rect){.x=0,.y=70+CHAR_HEIGHT*UI_TEXT_SCALE,.w=240,.h=30}, task_table_label, strlen(task_table_label), COLOR_LABEL_CHROME, false);
    }
    else {
        const char *task_label = "NONE";
        draw_label(display, (rect){.x=0,.y=70,.w=240,.h=30}, task_label, strlen(task_label), COLOR_LABEL_CHROME, false);
    }

    if (state == UI_TASK_STATE_IN_PROGRESS) {
        draw_button_complete(display);
    } else if (state == UI_TASK_STATE_READY) {
        draw_button_start(display);
    } else {
        draw_button(display, MAIN_START_BTN, "Start", BTN_DISABLED);
    }

    draw_bottom_button_layout(display, snapshot.has_task && snapshot.task_kind == MONITOR_TABLE, snapshot.has_task);

    draw_battery_icon(display, battery_monitor_get_bars());
}


rect table_tile_rect(uint8_t index) {
    uint8_t col = index % 3;
    uint8_t row = index / 3;

    return (rect) {
        .x = UI_TILE_START_X + col * (UI_TILE_W + UI_TILE_GAP_X),
        .y = UI_TILE_START_Y + row * (UI_TILE_H + UI_TILE_GAP_Y),
        .w = UI_TILE_W,
        .h = UI_TILE_H
    };
}


void ui_draw_grid(spi_device_handle_t display) {
    const uint8_t num_pages   = (NUM_OF_TABLES + TABLES_PER_PAGE - 1) / TABLES_PER_PAGE;
    const uint8_t page_start  = UI_GRID_PAGE * TABLES_PER_PAGE;

    const char *title_label = "Tables";
    const char *prev_label  = "< Prev";
    const char *next_label  = "Next >";

    display_fill(display, BG);

    draw_back_icon(display);
    draw_label(display, (rect){.x=0,.y=0,.w=UI_SCREEN_W,.h=UI_TOPBAR_H},
               title_label, strlen(title_label), COLOR_LABEL_CHROME, false);

    for (uint8_t slot = 0; slot < TABLES_PER_PAGE; ++slot) {
        uint8_t table_index = page_start + slot;
        if (table_index >= NUM_OF_TABLES) break;

        table_state tile_state    = system_get_table_state(table_index);
        task_kind tile_task_kind  = system_get_current_task_kind_for_table(table_index);
        uint16_t color_tile       = (tile_state == TABLE_DINING) ? GREEN : task_kind_tile_color(tile_task_kind);
        uint16_t label_color = (color_tile == DARK_GREY || color_tile == RED) ? WHITE : BLACK;

        rect tile = table_tile_rect(slot);
        draw_filled_rect(display, tile.x, tile.y, tile.w, tile.h, color_tile, 10);

        char table_label[4];
        snprintf(table_label, sizeof(table_label), "T%u", table_index + 1);
        draw_label(display, tile, table_label, strlen(table_label), label_color, false);
    }

    uint16_t prev_color = (UI_GRID_PAGE > 0)             ? LIGHT_GREY : DARK_GREY;
    uint16_t next_color = (UI_GRID_PAGE < num_pages - 1) ? LIGHT_GREY : DARK_GREY;

    draw_filled_rect(display, TABLE_GRID_PREV_BTN.x, TABLE_GRID_PREV_BTN.y, TABLE_GRID_PREV_BTN.w, TABLE_GRID_PREV_BTN.h, prev_color, 0);
    draw_label(display, TABLE_GRID_PREV_BTN, prev_label, strlen(prev_label), COLOR_LABEL_CHROME, false);

    draw_filled_rect(display, TABLE_GRID_NEXT_BTN.x, TABLE_GRID_NEXT_BTN.y, TABLE_GRID_NEXT_BTN.w, TABLE_GRID_NEXT_BTN.h, next_color, 0);
    draw_label(display, TABLE_GRID_NEXT_BTN, next_label, strlen(next_label), COLOR_LABEL_CHROME, false);

    draw_battery_icon(display, battery_monitor_get_bars());
}


void draw_active_table_page(spi_device_handle_t display_handle, uint8_t table_index) {
    display_fill(display_handle, BG);

    draw_back_icon(display_handle);

    char table_number_label[10];
    snprintf(table_number_label, sizeof(table_number_label), "Table %d", table_index + 1);
    draw_label(display_handle, (rect){.x=0,.y=0,.w=UI_SCREEN_W,.h=UI_TOPBAR_H},
               table_number_label, strlen(table_number_label), WHITE, false);

    const table_context *tbl = system_get_table(table_index);
    time_ms now = get_time();
    table_state tbl_state = tbl ? tbl->state : TABLE_IDLE;

    const char *state_name = table_state_to_str(tbl_state);
    task_kind tbl_task_kind = system_get_current_task_kind_for_table(table_index);
    rect state_rect = {.x=10,.y=35,.w=220,.h=40};
    if (tbl_task_kind != TASK_NOT_APPLICABLE) {
        draw_urgency_icon(display_handle, state_rect, strlen(state_name), task_kind_tile_color(tbl_task_kind));
    }
    draw_label(display_handle, state_rect, state_name, strlen(state_name), COLOR_LABEL_CHROME, false);

    char elapsed_str[16];
    if (tbl) {
        format_elapsed(now - tbl->state_entered_at, elapsed_str, sizeof(elapsed_str));
    } else {
        snprintf(elapsed_str, sizeof(elapsed_str), "?");
    }
    draw_label(display_handle, (rect){.x=10,.y=78,.w=220,.h=30},
               elapsed_str, strlen(elapsed_str), LIGHT_GREY, false);

    bool take_order_enabled = state_can_take_order(tbl_state);
    bool bill_enabled       = state_can_request_bill(tbl_state);
    bool undo_enabled       = table_can_undo(tbl);

    draw_button(display_handle, TABLE_INFO_TAKE_ORDER_BTN, "Take Order",
                take_order_enabled ? BTN_PRIMARY  : BTN_DISABLED);
    draw_button(display_handle, TABLE_INFO_BILL_BTN, "Bill",
                bill_enabled       ? BTN_SECONDARY : BTN_DISABLED);
    draw_button(display_handle, TABLE_INFO_UNDO_BTN, "Undo",
                undo_enabled       ? BTN_SECONDARY : BTN_DISABLED);

    draw_battery_icon(display_handle, battery_monitor_get_bars());
}