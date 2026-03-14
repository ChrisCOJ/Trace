#include "esp_log.h"

#include "../include/user_interface.h"
#include "../include/trace_system.h"
#include "../include/table_fsm.h"
#include "../include/trace_scheduler.h"
#include "../include/types.h"
#include "../include/display_util.h"
#include "../include/touch_controller_util.h"
#include "../include/font5x7.h"
#include "../include/haptic_driver.h"
#include "../include/battery_monitor.h"

#include "driver/spi_master.h"
#include <math.h>
#include <string.h>



enum {
    UI_SCREEN_W = DISPLAY_WIDTH,
    UI_SCREEN_H = DISPLAY_HEIGHT,

    UI_MARGIN_X = 10,
    UI_SMALL_GAP = 5,
    UI_MEDIUM_GAP = 10,
    UI_LARGE_GAP = 20,

    UI_TOPBAR_Y = 0,
    UI_TOPBAR_H = 30,
    UI_TOPBAR_X = 60,
    UI_TOPBAR_W = 120,

    UI_ACTION_BTN_Y = 140,
    UI_BOTTOM_BTN_Y = 215,
    UI_BOTTOM_BACK_Y = 245,

    UI_FULL_BTN_W = 220,
    UI_HALF_BTN_W = 100,
    UI_BTN_H = 60,
    UI_BACK_BTN_H = 35,

    UI_TILE_W = 60,
    UI_TILE_H = 60,
    UI_TILE_GAP_X = 20,
    UI_TILE_GAP_Y = 10,
    UI_TILE_START_X = 10,
    UI_TILE_START_Y = 35,

    UI_TABLE_INFO_BTN_H = 50,
    UI_TABLE_INFO_TAKE_ORDER_Y = 160,
    UI_TABLE_INFO_TAKE_ORDER_W = UI_HALF_BTN_W,
    UI_TABLE_INFO_BILL_Y = 160,
    UI_TABLE_INFO_BILL_W = UI_HALF_BTN_W,
    UI_TABLE_INFO_BACK_Y = 225,

    UI_TOUCH_PAD_X = 10,
    UI_TOUCH_PAD_Y = 5,

    UI_CORNER_RADIUS = 10,
    UI_TEXT_VERTICAL_SPACING = 7,

    UI_NO_TABLE_SELECTED = 0xFF,

    UI_GRID_NAV_BTN_W = 120,   // Prev / Next each take half the bottom strip

    UI_TOPBAR_BACK_W = 55,     // Consistent top-left back button width on grid/info screens

    // Sleep gesture: top-right corner zone (x >= MIN_X, y <= MAX_Y)
    UI_SLEEP_CORNER_MIN_X = 195,
    UI_SLEEP_CORNER_MAX_Y = 40,
    UI_SLEEP_HOLD_MS      = 1000,
    UI_INACTIVITY_SLEEP_MS = 30000,

    // Battery icon (top-right corner)
    UI_BATT_X       = 195,
    UI_BATT_Y       = 8,
    UI_BATT_W       = 30,
    UI_BATT_H       = 16,
    UI_BATT_TIP_W   = 3,
    UI_BATT_TIP_H   = 7,
    UI_BATT_BORDER  = 2,
    UI_BATT_BARS    = 4,
} UI_DIMENSION_CONSTANTS;


enum {
    WHITE = 0xFFFF,
    BLACK = 0x0000,

    RED         = 0xF800,
    GREEN       = 0x07E0,
    BLUE        = 0x001F,

    YELLOW      = 0xFFE0,
    CYAN        = 0x07FF,
    MAGENTA     = 0xF81F,

    GREY        = 0x39E7,
    LIGHT_GREY  = 0x7BEF,
    DARK_GREY   = 0x2104,

    ORANGE      = 0xFD20,
    BROWN       = 0xA145,
    PINK        = 0xFC18,
    PURPLE      = 0x8010,

    NAVY        = 0x000F,
    TEAL        = 0x0410,
    OLIVE       = 0x8400,

    SILVER      = 0xC618,
    MAROON      = 0x8000,
    LIME        = 0x07E0,
    AQUA        = 0x07FF
} UI_RGB565_CONSTANTS;


enum {
    UI_TEXT_SCALE       = 2,
    UI_TITLE_SCALE      = 2,
    UI_SMALL_TEXT_SCALE = 1,
} UI_FONT_SCALE;


static const uint8_t NUM_OF_TABLES      = 24;
static const uint8_t TABLES_PER_PAGE    = 9;

static volatile ui_mode UI_MODE = UI_MODE_TABLE_GRID;
static volatile ui_snapshot UI_SNAPSHOT;
static uint8_t UI_GRID_PAGE = 0;

static const char *TAG_UI = "ui";
/* Simple edge detector to avoid repeated triggers */
static bool last_touch_pressed = false;

// Label colours
static const uint16_t COLOR_LABEL_DEFAULT      = BLACK;
static const uint16_t COLOR_LABEL_ALTERNATIVE  = WHITE;

const uint16_t BG                              = BLACK;


static const task_id UNINITIALISED_TASK_ID = { UINT16_MAX, UINT16_MAX };


// State of current task, ready tasks can be switched, tasks in progress cannot and will remain on the UI until ready
typedef enum {
    UI_TASK_STATE_IDLE = 0,        // no task available
    UI_TASK_STATE_READY,           // task visible, can be started
    UI_TASK_STATE_IN_PROGRESS      // started, waiting for completion
} ui_task_state;

static ui_task_state UI_TASK_STATE = UI_TASK_STATE_IDLE;
static task_id UI_LOCKED_TASK_ID = { UINT16_MAX, 0 };


typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} rect;


/* Button layout (screen-space coordinates) */
static const rect MAIN_IGNORE_BTN = {
    .x = UI_MARGIN_X,
    .y = UI_BOTTOM_BTN_Y,
    .w = UI_FULL_BTN_W,
    .h = UI_BTN_H
};
static const rect MAIN_BILL_BTN = { 
    .x = UI_MARGIN_X,
    .y = UI_BOTTOM_BTN_Y,
    .w = UI_HALF_BTN_W,
    .h = UI_BTN_H
};

static const rect MAIN_START_BTN = {
    .x = UI_MARGIN_X,
    .y = UI_ACTION_BTN_Y,
    .w = UI_FULL_BTN_W,
    .h = UI_BTN_H
};
static const rect MAIN_COMPLETE_BTN = {
    .x = UI_MARGIN_X,
    .y = UI_ACTION_BTN_Y,
    .w = UI_FULL_BTN_W,
    .h = UI_BTN_H
};

static const rect MAIN_TAKEORDER_BTN = {
    .x = UI_SCREEN_W - UI_MARGIN_X - UI_HALF_BTN_W,
    .y = UI_BOTTOM_BTN_Y,
    .w = UI_HALF_BTN_W,
    .h = UI_BTN_H
};

/* Top bar button to open table grid */
static const rect MAIN_TABLES_BTN = {
    .x = UI_TOPBAR_X,
    .y = UI_TOPBAR_Y,
    .w = UI_TOPBAR_W,
    .h = UI_TOPBAR_H
};

static const rect TABLE_INFO_TAKE_ORDER_BTN = { 
    .x = UI_MARGIN_X, 
    .y = UI_TABLE_INFO_TAKE_ORDER_Y, 
    .w = UI_HALF_BTN_W, 
    .h = UI_TABLE_INFO_BTN_H };

static const rect TABLE_INFO_BILL_BTN = { 
    .x = UI_MARGIN_X + UI_TABLE_INFO_TAKE_ORDER_W + UI_LARGE_GAP, 
    .y = UI_TABLE_INFO_BILL_Y, 
    .w = UI_HALF_BTN_W, 
    .h = UI_TABLE_INFO_BTN_H };

/* Consistent top-left back button — same position on grid and table-info screens */
static const rect TOPBAR_BACK_BTN = {
    .x = 0,
    .y = UI_TOPBAR_Y,
    .w = UI_TOPBAR_BACK_W,
    .h = UI_TOPBAR_H
};

/* Undo button — full-width at the bottom of the table-info screen */
static const rect TABLE_INFO_UNDO_BTN = {
    .x = UI_MARGIN_X,
    .y = UI_TABLE_INFO_BACK_Y,
    .w = UI_FULL_BTN_W,
    .h = UI_TABLE_INFO_BTN_H };

/* Prev / Next navigation strip on grid screen (no Back — use TOPBAR_BACK_BTN) */
static const rect TABLE_GRID_PREV_BTN = {
    .x = 0,
    .y = UI_BOTTOM_BACK_Y,
    .w = UI_GRID_NAV_BTN_W,
    .h = UI_BACK_BTN_H
};
static const rect TABLE_GRID_NEXT_BTN = {
    .x = UI_GRID_NAV_BTN_W,
    .y = UI_BOTTOM_BACK_Y,
    .w = UI_GRID_NAV_BTN_W,
    .h = UI_BACK_BTN_H
};



/* --------------------------- Internal functions --------------------------- */

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

/* Basic point-in-rectangle test */
static inline bool point_in_rect(uint16_t x, uint16_t y, rect region)
{
    const int x_padding = 10;
    const int y_padding = 5;

    int x_left = (int)region.x - x_padding;
    if (x_left < 0) x_left = 0;
    int x_right = (int)region.x + region.w + x_padding;
    if (x_right >= DISPLAY_WIDTH) x_right = DISPLAY_WIDTH;
    int y_top = (int)region.y - y_padding;
    if (y_top < 0) y_top = 0;
    int y_bottom = (int)region.y + region.h + y_padding;
    if (y_bottom >= DISPLAY_HEIGHT) y_bottom = DISPLAY_HEIGHT;

    return (x >= x_left) && (x < x_right) &&
           (y >= y_top) && (y < y_bottom);
}


static void ui_update_snapshot_from_system() {
    const task *t = system_get_active_task();
    if (!t) {
        UI_SNAPSHOT.has_task = false;
        UI_SNAPSHOT.task_id = INVALID_TASK_ID;
        UI_SNAPSHOT.table_number = 0;
        UI_SNAPSHOT.task_kind = 0;
        return;
    }

    UI_SNAPSHOT.has_task = true;
    UI_SNAPSHOT.task_id = t->id;
    UI_SNAPSHOT.table_number = t->table_number;
    UI_SNAPSHOT.task_kind = t->kind;
}


/* ------------ Draw functions ------------ */

static void draw_back_icon(spi_device_handle_t display) {
    const uint16_t icon_x = 8;
    const uint16_t icon_y = (UI_TOPBAR_H - CHAR_HEIGHT * UI_TEXT_SCALE) / 2;
    draw_text(display, icon_x, icon_y, "<", WHITE, UI_TEXT_SCALE);
}

static void draw_label(spi_device_handle_t display, rect r, const char *label, size_t label_len, uint16_t text_color, bool snap_left) {
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
            // Cut the text short. To do.
            return;
        }
    }

    uint16_t text_y = r.y + r.h/2 - CHAR_HEIGHT * UI_TEXT_SCALE/2;
    uint16_t text_x = snap_left ? r.x : r.x + r.w/2 - label_len * CHAR_WIDTH * UI_TEXT_SCALE/2;

    draw_text(display, text_x, text_y, label, text_color, UI_TEXT_SCALE);
}


/* Draw a filled rectangle using line-by-line writes. Includes option to round corners */
static void draw_filled_rect(spi_device_handle_t display,
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

        // Clear corner pixels
        if (x_offset_buf[row] != 0) {
            for (int i = 0; i < x_offset_buf[row]; ++i) {
                scanline_buffer[i] = 0;
            }
            for (int i = width - x_offset_buf[row]; i < width; ++i) {
                scanline_buffer[i] = 0;
            }
        }

        for (int i = x_offset_buf[row]; i < width - x_offset_buf[row]; ++i) {
            scanline_buffer[i] = color_rgb565;
        }
        display_write(display, x, (uint16_t)(y + row), width, 1, scanline_buffer);
    }
}


static void draw_battery_icon(spi_device_handle_t display, uint8_t bars) {
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


static void draw_button_complete(spi_device_handle_t display) {
    const uint16_t COLOR_COMP = GREEN;
    const char *complete_label = "Complete";

    draw_filled_rect(display, MAIN_COMPLETE_BTN.x, MAIN_COMPLETE_BTN.y, MAIN_COMPLETE_BTN.w, MAIN_COMPLETE_BTN.h, COLOR_COMP, 10);
    draw_label(display, MAIN_COMPLETE_BTN, complete_label, strlen(complete_label), COLOR_LABEL_DEFAULT, false);
}


static void draw_button_start(spi_device_handle_t display) {
    const uint16_t COLOR_START = GREEN;
    const char *start_label = "Start";

    draw_filled_rect(display, MAIN_START_BTN.x, MAIN_START_BTN.y, MAIN_START_BTN.w, MAIN_START_BTN.h, COLOR_START, 10);
    draw_label(display, MAIN_START_BTN, start_label, strlen(start_label), COLOR_LABEL_DEFAULT, false);
}


static void draw_button_bill(spi_device_handle_t display) {
    const uint16_t COLOR_BILL = YELLOW;
    const char *bill_label = "Bill";

    draw_filled_rect(display, MAIN_BILL_BTN.x, MAIN_BILL_BTN.y, MAIN_BILL_BTN.w, MAIN_BILL_BTN.h, COLOR_BILL, 10);
    draw_label(display, MAIN_BILL_BTN, bill_label, strlen(bill_label), COLOR_LABEL_ALTERNATIVE, false);
}


static void draw_button_ignore(spi_device_handle_t display) {
    const uint16_t COLOR_IGNORE = GREY;
    const char *ignore_label = "Ignore";

    draw_filled_rect(display, MAIN_IGNORE_BTN.x, MAIN_IGNORE_BTN.y, MAIN_IGNORE_BTN.w, MAIN_IGNORE_BTN.h, COLOR_IGNORE, 10);
    draw_label(display, MAIN_IGNORE_BTN, ignore_label, strlen(ignore_label), COLOR_LABEL_ALTERNATIVE, false);
}


static void draw_button_take_order(spi_device_handle_t display) {
    const uint16_t COLOR_TAKE_ORDER = GREY;
    const char *take_order_label = "Take Order";

    draw_filled_rect(display, MAIN_TAKEORDER_BTN.x, MAIN_TAKEORDER_BTN.y, MAIN_TAKEORDER_BTN.w, MAIN_TAKEORDER_BTN.h, COLOR_TAKE_ORDER, 10);
    draw_label(display, MAIN_TAKEORDER_BTN, take_order_label, strlen(take_order_label), COLOR_LABEL_ALTERNATIVE, false);
}


static void draw_bottom_button_layout(spi_device_handle_t display_handle, bool monitor) {
    // Clear previous buttons in the draw area
    rect bg_clear_rect = { .x = 0,  .y = 215,  .w = 240, .h = 60 };
    draw_filled_rect(display_handle, bg_clear_rect.x, bg_clear_rect.y, bg_clear_rect.w, bg_clear_rect.h, BLACK, 0);

    if (monitor) {
        draw_button_bill(display_handle);
        draw_button_take_order(display_handle);
    }
    else {
        draw_button_ignore(display_handle);
    }
}


static void draw_active_task_label(spi_device_handle_t display, ui_snapshot snap) {
    rect task_label_rect = {.x=0, .y=50, .w=240, .h=70};
    draw_filled_rect(display, task_label_rect.x, task_label_rect.y, task_label_rect.w, task_label_rect.h, BG, 0);

    if (snap.has_task) {
        const char *task_kind_label = task_kind_to_str(snap.task_kind);
        draw_label(display, (rect){.x=0,.y=60,.w=240,.h=30}, task_kind_label, strlen(task_kind_label), COLOR_LABEL_ALTERNATIVE, false);

        char task_table_label[10];
        snprintf(task_table_label, sizeof(task_table_label), "Table %d", snap.table_number + 1);
        draw_label(display, (rect){.x=0,.y=70+CHAR_HEIGHT*UI_TEXT_SCALE,.w=240,.h=30}, task_table_label, strlen(task_table_label), COLOR_LABEL_ALTERNATIVE, false);
    }
    else {
        const char *task_label = "NONE";
        draw_label(display, (rect){.x=0,.y=70,.w=240,.h=30}, task_label, strlen(task_label), COLOR_LABEL_ALTERNATIVE, false);
    }
}


static void ui_draw_main(spi_device_handle_t display) {
    const uint16_t COLOR_TOPBAR = GREY;
    const char *tables_label = "Tables";

    display_fill(display, BG);

    // top bar
    draw_filled_rect(display, MAIN_TABLES_BTN.x, MAIN_TABLES_BTN.y, MAIN_TABLES_BTN.w, MAIN_TABLES_BTN.h, COLOR_TOPBAR, 10);
    draw_label(display, MAIN_TABLES_BTN, tables_label, strlen(tables_label), COLOR_LABEL_ALTERNATIVE, false);

    if (UI_SNAPSHOT.has_task) {
        const char *task_kind_label = task_kind_to_str(UI_SNAPSHOT.task_kind);
        draw_label(display, (rect){.x=0,.y=60,.w=240,.h=30}, task_kind_label, strlen(task_kind_label), COLOR_LABEL_ALTERNATIVE, false);

        char task_table_label[10];
        snprintf(task_table_label, sizeof(task_table_label), "Table %d", UI_SNAPSHOT.table_number + 1);
        draw_label(display, (rect){.x=0,.y=70+CHAR_HEIGHT*UI_TEXT_SCALE,.w=240,.h=30}, task_table_label, strlen(task_table_label), COLOR_LABEL_ALTERNATIVE, false);

        if (UI_TASK_STATE == UI_TASK_STATE_READY) {
            draw_button_start(display);
        }
        else if (UI_TASK_STATE == UI_TASK_STATE_IN_PROGRESS) {
            draw_button_complete(display);
        }
        
        draw_bottom_button_layout(display, UI_SNAPSHOT.task_kind == MONITOR_TABLE);
    }
    else {
        const char *task_label = "NONE";
        draw_label(display, (rect){.x=0,.y=70,.w=240,.h=30}, task_label, strlen(task_label), COLOR_LABEL_ALTERNATIVE, false);
    }

    draw_battery_icon(display, battery_monitor_get_bars());
}


static rect table_tile_rect(uint8_t index) {
    uint8_t col = index % 3;
    uint8_t row = index / 3;

    return (rect) {
        .x = UI_TILE_START_X + col * (UI_TILE_W + UI_TILE_GAP_X),
        .y = UI_TILE_START_Y + row * (UI_TILE_H + UI_TILE_GAP_Y),
        .w = UI_TILE_W,
        .h = UI_TILE_H
    };
}


static void ui_draw_grid(spi_device_handle_t display) {
    const uint8_t num_pages   = (NUM_OF_TABLES + TABLES_PER_PAGE - 1) / TABLES_PER_PAGE;
    const uint8_t page_start  = UI_GRID_PAGE * TABLES_PER_PAGE;

    const char *title_label = "Select Table";
    const char *prev_label  = "< Prev";
    const char *next_label  = "Next >";

    display_fill(display, BG);

    draw_back_icon(display);
    draw_label(display, (rect){.x=0,.y=0,.w=UI_SCREEN_W,.h=UI_TOPBAR_H},
               title_label, strlen(title_label), COLOR_LABEL_ALTERNATIVE, false);

    for (uint8_t slot = 0; slot < TABLES_PER_PAGE; ++slot) {
        uint8_t table_index = page_start + slot;
        if (table_index >= NUM_OF_TABLES) break;

        table_state table_current_state = system_get_table_state(table_index);
        uint16_t color_tile = (table_current_state == TABLE_IDLE) ? LIGHT_GREY : YELLOW;

        rect tile = table_tile_rect(slot);
        draw_filled_rect(display, tile.x, tile.y, tile.w, tile.h, color_tile, 10);

        char table_label[4];
        snprintf(table_label, sizeof(table_label), "T%u", table_index + 1);
        draw_label(display, tile, table_label, strlen(table_label), COLOR_LABEL_DEFAULT, false);
    }

    uint16_t prev_color = (UI_GRID_PAGE > 0)             ? LIGHT_GREY : DARK_GREY;
    uint16_t next_color = (UI_GRID_PAGE < num_pages - 1) ? LIGHT_GREY : DARK_GREY;

    draw_filled_rect(display, TABLE_GRID_PREV_BTN.x, TABLE_GRID_PREV_BTN.y, TABLE_GRID_PREV_BTN.w, TABLE_GRID_PREV_BTN.h, prev_color, 0);
    draw_label(display, TABLE_GRID_PREV_BTN, prev_label, strlen(prev_label), COLOR_LABEL_ALTERNATIVE, false);

    draw_filled_rect(display, TABLE_GRID_NEXT_BTN.x, TABLE_GRID_NEXT_BTN.y, TABLE_GRID_NEXT_BTN.w, TABLE_GRID_NEXT_BTN.h, next_color, 0);
    draw_label(display, TABLE_GRID_NEXT_BTN, next_label, strlen(next_label), COLOR_LABEL_ALTERNATIVE, false);

    draw_battery_icon(display, battery_monitor_get_bars());
}


static inline bool state_can_take_order(table_state state) {
    return state == TABLE_SEATED || state == TABLE_PLACED_ORDER ||
           state == TABLE_WAITING_FOR_ORDER || state == TABLE_DINING || state == TABLE_CHECKUP;
}

static inline bool state_can_request_bill(table_state state) {
    return state == TABLE_WAITING_FOR_ORDER || state == TABLE_DINING || state == TABLE_CHECKUP;
}

static inline bool table_can_undo(const table_context *tbl) {
    return tbl != NULL && tbl->prev_state != TABLE_IDLE;
}


static void draw_active_table_page(spi_device_handle_t display_handle, uint8_t table_index) {
    const uint16_t color_table_number = YELLOW;

    display_fill(display_handle, BG);

    draw_back_icon(display_handle);

    char table_number_label[10];
    snprintf(table_number_label, sizeof(table_number_label), "Table %d", table_index + 1);
    draw_label(display_handle, (rect){.x=0,.y=0,.w=UI_SCREEN_W,.h=UI_TOPBAR_H},
               table_number_label, strlen(table_number_label), color_table_number, false);

    const char *current_task_title = "Task: ";
    const rect current_task_title_rect = {.x=10, .y=50, .w=80, .h=10};
    draw_label(display_handle, current_task_title_rect, current_task_title, strlen(current_task_title), COLOR_LABEL_ALTERNATIVE, true);

    char current_task_label[20];
    const rect current_task_label_rect = {.x=(current_task_title_rect.x + current_task_title_rect.w) - 20, .y=50, .w=(240 - current_task_title_rect.w), .h=10};
    task_kind current_task = system_get_current_task_kind_for_table(table_index);
    if (current_task == TASK_NOT_APPLICABLE) {
        snprintf(current_task_label, sizeof(current_task_label), "None");
    }
    else {
        snprintf(current_task_label, sizeof(current_task_label), "%s", task_kind_to_str(current_task));
    }
    draw_label(display_handle, current_task_label_rect, current_task_label, strlen(current_task_label), COLOR_LABEL_ALTERNATIVE, true);

    const table_context *tbl = system_get_table(table_index);
    time_ms now = get_time();

    char stage_label[32];
    snprintf(stage_label, sizeof(stage_label), "Stage: %s", tbl ? table_state_to_str(tbl->state) : "?");
    const rect stage_rect = {.x=10, .y=80, .w=220, .h=10};
    draw_label(display_handle, stage_rect, stage_label, strlen(stage_label), COLOR_LABEL_ALTERNATIVE, true);

    char stage_time_label[32];
    if (tbl) {
        char elapsed_str[16];
        format_elapsed(now - tbl->state_entered_at, elapsed_str, sizeof(elapsed_str));
        snprintf(stage_time_label, sizeof(stage_time_label), "In stage: %s", elapsed_str);
    } else {
        snprintf(stage_time_label, sizeof(stage_time_label), "In stage: ?");
    }
    const rect stage_time_rect = {.x=10, .y=110, .w=220, .h=10};
    draw_label(display_handle, stage_time_rect, stage_time_label, strlen(stage_time_label), COLOR_LABEL_ALTERNATIVE, true);

    table_state tbl_state = tbl ? tbl->state : TABLE_IDLE;
    bool take_order_enabled = state_can_take_order(tbl_state);
    bool bill_enabled = state_can_request_bill(tbl_state);

    const uint16_t take_order_btn_color  = take_order_enabled ? YELLOW     : DARK_GREY;
    const uint16_t take_order_text_color = take_order_enabled ? COLOR_LABEL_DEFAULT : GREY;
    const char *take_order_label = "Take Order";
    draw_filled_rect(display_handle, TABLE_INFO_TAKE_ORDER_BTN.x, TABLE_INFO_TAKE_ORDER_BTN.y,
                     TABLE_INFO_TAKE_ORDER_BTN.w, TABLE_INFO_TAKE_ORDER_BTN.h, take_order_btn_color, 10);
    draw_label(display_handle, TABLE_INFO_TAKE_ORDER_BTN, take_order_label, strlen(take_order_label),
               take_order_text_color, false);

    const uint16_t bill_btn_color  = bill_enabled ? YELLOW     : DARK_GREY;
    const uint16_t bill_text_color = bill_enabled ? COLOR_LABEL_DEFAULT : GREY;
    const char *bill_label = "Bill";
    draw_filled_rect(display_handle, TABLE_INFO_BILL_BTN.x, TABLE_INFO_BILL_BTN.y,
                     TABLE_INFO_BILL_BTN.w, TABLE_INFO_BILL_BTN.h, bill_btn_color, 10);
    draw_label(display_handle, TABLE_INFO_BILL_BTN, bill_label, strlen(bill_label),
               bill_text_color, false);

    bool undo_enabled = table_can_undo(tbl);
    const uint16_t undo_btn_color  = undo_enabled ? ORANGE    : DARK_GREY;
    const uint16_t undo_text_color = undo_enabled ? COLOR_LABEL_DEFAULT : GREY;
    const char *undo_label = "Undo";
    draw_filled_rect(display_handle, TABLE_INFO_UNDO_BTN.x, TABLE_INFO_UNDO_BTN.y,
                     TABLE_INFO_UNDO_BTN.w, TABLE_INFO_UNDO_BTN.h, undo_btn_color, 10);
    draw_label(display_handle, TABLE_INFO_UNDO_BTN, undo_label, strlen(undo_label),
               undo_text_color, false);


    draw_battery_icon(display_handle, battery_monitor_get_bars());
}


static inline bool task_id_equal(task_id a, task_id b) {
    return a.index == b.index && a.generation == b.generation;
}


/* ------------ Button callbacks ------------ */
static ui_action decode_touch_main(uint16_t x, uint16_t y, task_kind kind) {
    if (point_in_rect(x, y, MAIN_IGNORE_BTN) && kind != MONITOR_TABLE) {
        return UI_ACTION_IGNORE;
    }

    if (point_in_rect(x, y, MAIN_BILL_BTN) && kind == MONITOR_TABLE) {
        return UI_ACTION_BILL;
    }

    if (point_in_rect(x, y, MAIN_START_BTN) && UI_TASK_STATE == UI_TASK_STATE_READY) {
        return UI_ACTION_START_TASK;
    }

    if (point_in_rect(x, y, MAIN_COMPLETE_BTN) && UI_TASK_STATE == UI_TASK_STATE_IN_PROGRESS) {
        return UI_ACTION_COMPLETE;
    }

    if (point_in_rect(x, y, MAIN_TAKEORDER_BTN)) {
        return UI_ACTION_TAKE_ORDER;
    }

    if (point_in_rect(x, y, MAIN_TABLES_BTN)) {
        return UI_ACTION_OPEN_TABLES;
    }

    return UI_ACTION_NONE;
}


static ui_action decode_touch_grid(uint16_t x, uint16_t y) {
    if (point_in_rect(x, y, TOPBAR_BACK_BTN))     return UI_ACTION_BACK;
    if (point_in_rect(x, y, TABLE_GRID_PREV_BTN)) return UI_ACTION_GRID_PREV_PAGE;
    if (point_in_rect(x, y, TABLE_GRID_NEXT_BTN)) return UI_ACTION_GRID_NEXT_PAGE;

    for (uint8_t slot = 0; slot < TABLES_PER_PAGE; slot++) {
        rect tile = table_tile_rect(slot);
        if (point_in_rect(x, y, tile)) {
            return (ui_action)(UI_ACTION_TABLE_TILE_1 + slot);
        }
    }
    return UI_ACTION_NONE;
}


static ui_action decode_touch_table_info(uint16_t x, uint16_t y) {
    if (point_in_rect(x, y, TOPBAR_BACK_BTN))           return UI_ACTION_TABLE_INFO_BACK;
    if (point_in_rect(x, y, TABLE_INFO_TAKE_ORDER_BTN)) return UI_ACTION_TABLE_INFO_TAKE_ORDER;
    if (point_in_rect(x, y, TABLE_INFO_BILL_BTN))       return UI_ACTION_TABLE_INFO_BILL;
    if (point_in_rect(x, y, TABLE_INFO_UNDO_BTN))       return UI_ACTION_TABLE_INFO_UNDO;

    return UI_ACTION_NONE;
}



/* -------------------------- API -------------------------- */
void ui_draw_layout(spi_device_handle_t display) {
    if (UI_MODE == UI_MODE_MAIN) {
        ui_draw_main(display);
    } else {
        ui_draw_grid(display);
    }
}


void ui_task(void *arg) {
    display_spi_ctx display = *(display_spi_ctx *)arg;
    task_id prev_task_id = UNINITIALISED_TASK_ID;
    static uint8_t SELECTED_TABLE = 0xFF;
    uint8_t prev_bars = 0xFF;
    uint32_t batt_tick = 0;

    bool display_sleeping = false;
    time_ms last_activity_ms = get_time();

    // Top-right corner hold state for sleep gesture
    bool corner_hold_active = false;
    time_ms corner_hold_start_ms = 0;
    bool sleep_triggered_this_hold = false;

    while (1) {
        time_ms now = get_time();
        uint16_t x = 0, y = 0;
        bool pressed = read_touch_point(&x, &y);

        // --- Wake from sleep on any touch edge ---
        if (display_sleeping) {
            if (pressed && !last_touch_pressed) {
                display_sleeping = false;
                display_backlight_set(true);
                last_activity_ms = now;
                corner_hold_active = false;
                sleep_triggered_this_hold = false;
            }
            last_touch_pressed = pressed;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // --- Inactivity sleep (30 s with no touch) ---
        if ((now - last_activity_ms) >= UI_INACTIVITY_SLEEP_MS) {
            display_sleeping = true;
            display_backlight_set(false);
            corner_hold_active = false;
            sleep_triggered_this_hold = false;
            last_touch_pressed = pressed;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // --- Top-right corner hold-to-sleep gesture ---
        bool in_sleep_corner = pressed &&
                               (x >= UI_SLEEP_CORNER_MIN_X) &&
                               (y <= UI_SLEEP_CORNER_MAX_Y);

        if (in_sleep_corner && !corner_hold_active) {
            corner_hold_active = true;
            corner_hold_start_ms = now;
            sleep_triggered_this_hold = false;
        } else if (!pressed) {
            corner_hold_active = false;
            sleep_triggered_this_hold = false;
        }

        if (corner_hold_active && !sleep_triggered_this_hold) {
            if ((now - corner_hold_start_ms) >= UI_SLEEP_HOLD_MS) {
                sleep_triggered_this_hold = true;
                display_sleeping = true;
                display_backlight_set(false);
                last_touch_pressed = pressed;
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
        }

        if (pressed) {
            last_activity_ms = now;
        }

        // --- Normal UI update ---
        ui_update_snapshot_from_system();
        // Copy the snapshot once per press to avoid mixed fields
        ui_snapshot snap = UI_SNAPSHOT;

        if (UI_MODE == UI_MODE_MAIN && UI_TASK_STATE != UI_TASK_STATE_IN_PROGRESS) {
            if (!task_id_equal(snap.task_id, prev_task_id)) {
                if (snap.has_task) {
                    if (UI_TASK_STATE == UI_TASK_STATE_READY || UI_TASK_STATE == UI_TASK_STATE_IDLE) {
                        drv2605l_play_effect(1);
                    }
                    UI_TASK_STATE = UI_TASK_STATE_READY;
                    UI_LOCKED_TASK_ID = snap.task_id;
                    draw_button_start(display.dev_handle);
                } else {
                    UI_TASK_STATE = UI_TASK_STATE_IDLE;
                    UI_LOCKED_TASK_ID = INVALID_TASK_ID;
                }

                draw_bottom_button_layout(display.dev_handle, snap.task_kind == MONITOR_TABLE);
                draw_active_task_label(display.dev_handle, snap);
                prev_task_id = snap.task_id;
            }
        }

        if (pressed && !last_touch_pressed) {
            ui_action act = UI_ACTION_NONE;
            switch (UI_MODE) {
                case UI_MODE_MAIN: {
                    act = decode_touch_main(x, y, snap.task_kind);
                    switch (act) {
                        case UI_ACTION_OPEN_TABLES:
                            UI_GRID_PAGE = 0;
                            UI_MODE = UI_MODE_TABLE_GRID;
                            ui_draw_layout(display.dev_handle);
                            break;

                        case UI_ACTION_IGNORE:
                            if (snap.has_task) {
                                system_apply_user_action_to_task(snap.task_id, USER_ACTION_IGNORE, now);
                            }
                            break;

                        case UI_ACTION_BILL:
                                system_apply_table_fsm_event(snap.table_number, EVENT_TABLE_REQUESTED_BILL, now);
                            break;

                        case UI_ACTION_START_TASK:
                            if (snap.has_task) {
                                UI_TASK_STATE = UI_TASK_STATE_IN_PROGRESS;
                                UI_LOCKED_TASK_ID = snap.task_id;
                                draw_button_complete(display.dev_handle);
                            }
                            break;

                        case UI_ACTION_COMPLETE:
                            if (UI_TASK_STATE == UI_TASK_STATE_IN_PROGRESS) {
                                system_apply_user_action_to_task(UI_LOCKED_TASK_ID, USER_ACTION_COMPLETE, now);
                                UI_TASK_STATE = UI_TASK_STATE_IDLE;
                                UI_LOCKED_TASK_ID = INVALID_TASK_ID;
                                draw_button_start(display.dev_handle);
                            }
                            break;

                        case UI_ACTION_TAKE_ORDER:
                                system_apply_table_fsm_event(snap.table_number, EVENT_TAKE_ORDER_EARLY_OR_REPEAT, now);
                            break;

                        default:
                            break;
                    }
                } break;

                case UI_MODE_TABLE_GRID: {
                    act = decode_touch_grid(x, y);
                    const uint8_t num_pages = (NUM_OF_TABLES + TABLES_PER_PAGE - 1) / TABLES_PER_PAGE;

                    if (act == UI_ACTION_BACK) {
                        UI_MODE = UI_MODE_MAIN;
                        ui_draw_layout(display.dev_handle);
                        break;
                    }

                    if (act == UI_ACTION_GRID_PREV_PAGE && UI_GRID_PAGE > 0) {
                        UI_GRID_PAGE--;
                        ui_draw_grid(display.dev_handle);
                        break;
                    }

                    if (act == UI_ACTION_GRID_NEXT_PAGE && UI_GRID_PAGE < num_pages - 1) {
                        UI_GRID_PAGE++;
                        ui_draw_grid(display.dev_handle);
                        break;
                    }

                    if (act >= UI_ACTION_TABLE_TILE_1 && act <= UI_ACTION_TABLE_TILE_9) {
                        uint8_t slot = (uint8_t)(act - UI_ACTION_TABLE_TILE_1);
                        uint8_t table = UI_GRID_PAGE * TABLES_PER_PAGE + slot;

                        if (table >= NUM_OF_TABLES) break;

                        table_state tapped_table_state = system_get_table_state(table);
                        if (tapped_table_state == TABLE_IDLE) {
                            system_apply_table_fsm_event(table, EVENT_CUSTOMERS_SEATED, now);
                            UI_MODE = UI_MODE_MAIN;
                            ui_draw_layout(display.dev_handle);
                        } else {
                            SELECTED_TABLE = table;
                            UI_MODE = UI_MODE_TABLE_INFO;
                            draw_active_table_page(display.dev_handle, table);
                        }
                    }
                } break;

                case UI_MODE_TABLE_INFO: {
                    act = decode_touch_table_info(x, y);

                    if (act == UI_ACTION_TABLE_INFO_BACK) {
                        UI_MODE = UI_MODE_TABLE_GRID;
                        ui_draw_layout(display.dev_handle);
                        break;
                    }

                    if (act == UI_ACTION_TABLE_INFO_TAKE_ORDER &&
                        state_can_take_order(system_get_table_state(SELECTED_TABLE))) {
                        system_apply_table_fsm_event(SELECTED_TABLE, EVENT_TAKE_ORDER_EARLY_OR_REPEAT, now);
                        UI_MODE = UI_MODE_MAIN;
                        ui_draw_layout(display.dev_handle);
                    }

                    if (act == UI_ACTION_TABLE_INFO_BILL &&
                        state_can_request_bill(system_get_table_state(SELECTED_TABLE))) {
                        system_apply_table_fsm_event(SELECTED_TABLE, EVENT_TABLE_REQUESTED_BILL, now);
                        UI_MODE = UI_MODE_MAIN;
                        ui_draw_layout(display.dev_handle);
                    }

                    if (act == UI_ACTION_TABLE_INFO_UNDO &&
                        table_can_undo(system_get_table(SELECTED_TABLE))) {
                        system_apply_table_fsm_event(SELECTED_TABLE, EVENT_UNDO, now);
                        draw_active_table_page(display.dev_handle, SELECTED_TABLE);
                    }
                } break;

                default:
                    break;
            }

            ESP_LOGI(TAG_UI, "touch x=%u y=%u mode=%d act=%d",
                     (unsigned)x, (unsigned)y, (int)UI_MODE, (int)act);
        }

        last_touch_pressed = pressed;

        // Update battery reading every ~10 s (200 × 50 ms ticks)
        if (++batt_tick >= 200) {
            batt_tick = 0;
            battery_monitor_update();
        }

        uint8_t current_bars = battery_monitor_get_bars();
        if (current_bars != prev_bars) {
            draw_battery_icon(display.dev_handle, current_bars);
            prev_bars = current_bars;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
