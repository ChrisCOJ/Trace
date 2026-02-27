#include "esp_log.h"

#include "../include/user_interface.h"
#include "../include/trace_system.h"
#include "../include/table_fsm.h"
#include "../include/trace_scheduler.h"
#include "../include/types.h"
#include "../include/display_util.h"
#include "../include/touch_controller_util.h"
#include "../include/font5x7.h"

#include "driver/spi_master.h"
#include <math.h>
#include <string.h>


/* Used to switch between UI pages */
static volatile ui_mode UI_MODE = UI_MODE_TABLE_GRID;
/* Used to store the last drawn UI information */
static volatile ui_snapshot UI_SNAPSHOT;

static const char *TAG_UI = "ui";
/* Simple edge detector to avoid repeated triggers */
static bool last_touch_pressed = false;

// Label colours
static const uint16_t COLOR_LABEL_DEFAULT      = 0x0000;
static const uint16_t COLOR_LABEL_ALTERNATIVE  = 0xFFFF;

static bool TASK_STARTED                 = false;
static bool TASK_PRESS_COMPLETE          = true;

static uint8_t SCALE                     = 2;


/* Simple rectangular hit region */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} rect;


/* Button layout (screen-space coordinates) */
static const rect BUTTON_IGNORE         = { .x = 10,  .y = 200,  .w = 100, .h = 60 };
static const rect BUTTON_CLOSE_TABLE    = { .x = 10,  .y = 200,  .w = 100, .h = 60 };

static const rect BUTTON_START          = { .x = 10,  .y = 130, .w = 220, .h = 60 };
static const rect BUTTON_COMPLETE       = { .x = 10,  .y = 130, .w = 220, .h = 60 };

static const rect BUTTON_TAKEORDER      = { .x = 130, .y = 200,  .w = 100, .h = 60 };

/* Top bar button to open table grid */
static const rect BUTTON_TABLES   = { .x=10,  .y=10,   .w=220, .h=30 };

/* GRID screen: 9 table tiles (3 cols x 3 rows) */
static const rect TABLE_TILE[9] = {
    { .x=10,  .y=35,  .w=60, .h=60 }, { .x=90, .y=35, .w=60, .h=60 }, { .x=170, .y=35,  .w=60, .h=60 },
    { .x=10,  .y=105, .w=60, .h=60 }, { .x=90, .y=105, .w=60, .h=60 }, { .x=170, .y=105, .w=60, .h=60 },
    { .x=10,  .y=175, .w=60, .h=60 }, { .x=90, .y=175, .w=60, .h=60 }, { .x=170, .y=175, .w=60, .h=60 }
};

/* Back button on grid screen */
static const rect BUTTON_BACK     = { .x=10,  .y=245, .w=220, .h=30 };


/* Basic point-in-rectangle test */
static inline bool point_in_rect(uint16_t x, uint16_t y, rect region)
{
    return (x >= region.x) && (x < (uint16_t)(region.x + region.w)) &&
           (y >= region.y) && (y < (uint16_t)(region.y + region.h));
}


static void ui_update_snapshot_from_system() {
    const task *t = trace_system_get_active_task();
    if (!t) {
        UI_SNAPSHOT.has_task = false;
        UI_SNAPSHOT.task_id = (task_id){ .index = UINT16_MAX, .generation = 0 };
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

static void draw_label(spi_device_handle_t display, rect r, const char *label, size_t label_len, uint16_t text_color) {
    // Split label in parts and arrange vertically if too long for its container.
    if (label_len * CHAR_WIDTH * SCALE > r.w) {
        char label_cpy[32];
        strcpy(label_cpy, label);
        char *space = strchr(label_cpy, ' ');
        if (space) {
            const uint8_t VERTICAL_SPACING = 7;
            // Split the text vertically after the first space
            *space = '\0';  // NULL terminate the first word to split the text into two parts
            const char *first_part = label_cpy;  // Now label points to the first word in the string
            const char *second_part = space + 1;
            uint16_t first_part_x = r.x + r.w/2 - strlen(first_part) * CHAR_WIDTH * SCALE/2;
            uint16_t first_part_y = r.y + r.h/2 - (CHAR_HEIGHT * 2 + VERTICAL_SPACING) * SCALE / 2;
            uint16_t second_part_x = r.x + r.w/2 - strlen(second_part) * CHAR_WIDTH * SCALE/2;
            uint16_t second_part_y = first_part_y + CHAR_HEIGHT * SCALE + VERTICAL_SPACING;

            draw_text(display, first_part_x, first_part_y, first_part, text_color, SCALE);
            draw_text(display, second_part_x, second_part_y, second_part, text_color, SCALE);
            return;
        }
        else {
            // Cut the text short. To do.
            return;
        }
    }

    uint16_t text_x = r.x + r.w/2 - label_len * CHAR_WIDTH * SCALE/2;
    uint16_t text_y = r.y + r.h/2 - CHAR_HEIGHT * SCALE/2;

    draw_text(display, text_x, text_y, label, text_color, SCALE);
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

    // Cap rounding radius to half of the shortest side of the rectangle.
    uint16_t max_radius = (width < height ? width : height) / 2;
    if (radius > max_radius) {
        radius = max_radius;
    }

    /* Build x offset buffer */
    static uint8_t x_offset_buf[DISPLAY_HEIGHT];
    // Calculate x offset of the top corners.
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
    
    // Calculate x offset of the bottom corners.
    for (int i = radius; i >= 0; --i) {
        uint8_t dy = radius - i;
        uint8_t dx = floor(sqrt(radius * radius - dy * dy));
        uint8_t x_offset = radius - dx;
        x_offset_buf[height - i - 1] = x_offset;
    }

    // Populate the scanline buffer
    for (int row = 0; row < height; ++row) {
        
        // Clear cut pixels around the corners
        if (x_offset_buf[row] != 0) {
            for (int i = 0; i < x_offset_buf[row]; ++i) {
                scanline_buffer[i] = 0;
            }
            for (int i = width - x_offset_buf[row] - 1; i < width; ++i) {
                scanline_buffer[i] = 0;
            }
        }

        for (int i = x_offset_buf[row]; i < width - x_offset_buf[row]; ++i) {
            scanline_buffer[i] = color_rgb565;
        }
        display_write(display, x, (uint16_t)(y + row), width, 1, scanline_buffer);
    }
}


static void draw_button_complete(spi_device_handle_t display) {
    const uint16_t COLOR_COMP = 0x07E0;
    const char *complete_label = "Complete";

    draw_filled_rect(display, BUTTON_COMPLETE.x, BUTTON_COMPLETE.y, BUTTON_COMPLETE.w, BUTTON_COMPLETE.h, COLOR_COMP, 10);
    draw_label(display, BUTTON_COMPLETE, complete_label, strlen(complete_label), COLOR_LABEL_DEFAULT);
}


static void draw_button_start(spi_device_handle_t display) {
    const uint16_t COLOR_START = 0x07E0;
    const char *start_label = "Start";

    draw_filled_rect(display, BUTTON_START.x, BUTTON_START.y, BUTTON_START.w, BUTTON_START.h, COLOR_START, 10);
    draw_label(display, BUTTON_START, start_label, strlen(start_label), COLOR_LABEL_DEFAULT);
}


static void draw_button_close_table(spi_device_handle_t display) {
    const uint16_t COLOR_CLOSE = 0xF800; // red
    const char *close_table = "Close Table";

    draw_filled_rect(display, BUTTON_CLOSE_TABLE.x, BUTTON_CLOSE_TABLE.y, BUTTON_CLOSE_TABLE.w, BUTTON_CLOSE_TABLE.h, COLOR_CLOSE, 10);
    draw_label(display, BUTTON_CLOSE_TABLE, close_table, strlen(close_table), COLOR_LABEL_ALTERNATIVE);
}


static void draw_button_ignore(spi_device_handle_t display) {
    const uint16_t COLOR_IGNORE = 0x39E7; // grey
    const char *ignore_label = "Ignore";

    draw_filled_rect(display, BUTTON_IGNORE.x, BUTTON_IGNORE.y, BUTTON_IGNORE.w, BUTTON_IGNORE.h, COLOR_IGNORE, 10);
    draw_label(display, BUTTON_IGNORE, ignore_label, strlen(ignore_label), COLOR_LABEL_ALTERNATIVE);
}


static void draw_active_task_label(spi_device_handle_t display, ui_snapshot snap) {
    if (snap.has_task) {
        const char *task_kind_label = task_kind_to_str(snap.task_kind);
        draw_label(display, (rect){.x=0,.y=60,.w=240,.h=30}, task_kind_label, strlen(task_kind_label), COLOR_LABEL_ALTERNATIVE);

        char task_table_label[10];
        snprintf(task_table_label, sizeof(task_table_label), "Table %d", snap.table_number + 1);
        draw_label(display, (rect){.x=0,.y=70+CHAR_HEIGHT*SCALE,.w=240,.h=30}, task_table_label, strlen(task_table_label), COLOR_LABEL_ALTERNATIVE);
    }
    else {
        const char *task_label = "NONE";
        draw_label(display, (rect){.x=0,.y=70,.w=240,.h=30}, task_label, strlen(task_label), COLOR_LABEL_ALTERNATIVE);
    }
}


static void ui_draw_main(spi_device_handle_t display) {
    const uint16_t BG                       = 0x0000;

    const uint16_t COLOR_TAKE               = 0x39E7;
    const uint16_t COLOR_TOPBAR             = 0x39E7; // grey

    const char *tables_label = "Tables";
    const char *take_order_label = "Take Order";

    display_fill(display, BG);

    // top bar
    draw_filled_rect(display, BUTTON_TABLES.x, BUTTON_TABLES.y, BUTTON_TABLES.w, BUTTON_TABLES.h, COLOR_TOPBAR, 10);
    draw_label(display, BUTTON_TABLES, tables_label, strlen(tables_label), COLOR_LABEL_ALTERNATIVE);

    // buttons
    if (UI_SNAPSHOT.task_kind == MONITOR_TABLE) {
        draw_button_close_table(display);
    }
    else {
        draw_button_ignore(display);
    }

    if (TASK_PRESS_COMPLETE) {
        draw_button_start(display);
    }
    else if (TASK_STARTED) {
        draw_button_complete(display);
    }

    draw_filled_rect(display, BUTTON_TAKEORDER.x, BUTTON_TAKEORDER.y, BUTTON_TAKEORDER.w, BUTTON_TAKEORDER.h, COLOR_TAKE, 10);
    draw_label(display, BUTTON_TAKEORDER, take_order_label, strlen(take_order_label), COLOR_LABEL_ALTERNATIVE);
}


static void ui_draw_grid(spi_device_handle_t display) {
    const uint16_t BG           = 0x0000;
    const uint16_t COLOR_TILE   = 0x7BEF; // light grey
    const uint16_t COLOR_BACK   = 0x39E7; // grey

    const char *select_table_label = "Select Table";
    const char *back_label = "Back";

    display_fill(display, BG);

    draw_label(display, (rect){.x=10,.y=0,.w=240,.h=30}, select_table_label, strlen(select_table_label), COLOR_LABEL_ALTERNATIVE);

    for (int i = 0; i < (sizeof(TABLE_TILE) / sizeof(TABLE_TILE[0])); ++i) {
        draw_filled_rect(display, TABLE_TILE[i].x, TABLE_TILE[i].y, TABLE_TILE[i].w, TABLE_TILE[i].h, COLOR_TILE, 10);
        
        char table_label[4];
        snprintf(table_label, sizeof(table_label), "T%d", i + 1);
        draw_label(display, TABLE_TILE[i], table_label, strlen(table_label), COLOR_LABEL_DEFAULT);
    }

    draw_filled_rect(display, BUTTON_BACK.x, BUTTON_BACK.y, BUTTON_BACK.w, BUTTON_BACK.h, COLOR_BACK, 10);
    draw_label(display, BUTTON_BACK, back_label, strlen(back_label), COLOR_LABEL_ALTERNATIVE);
}


static void ui_draw_take_order_grid(spi_device_handle_t display) {

}


static inline bool task_id_equal(task_id a, task_id b) {
    return a.index == b.index && a.generation == b.generation;
}

/* -------------------------- API -------------------------- */

void ui_draw_layout(spi_device_handle_t display) {
    if (UI_MODE == UI_MODE_MAIN) {
        ui_draw_main(display);
    } else {
        ui_draw_grid(display);
    }
}


ui_action decode_touch_main(uint16_t x, uint16_t y, task_kind kind) {
    if (point_in_rect(x, y, BUTTON_IGNORE) && kind != MONITOR_TABLE)      return UI_ACTION_IGNORE;
    if (point_in_rect(x, y, BUTTON_CLOSE_TABLE) && kind == MONITOR_TABLE) return UI_ACTION_CLOSE_TABLE;
    if (point_in_rect(x, y, BUTTON_COMPLETE) && !TASK_PRESS_COMPLETE) { 
        TASK_PRESS_COMPLETE = true;      
        TASK_STARTED = false;
        return UI_ACTION_COMPLETE;
    }
    if (point_in_rect(x, y, BUTTON_COMPLETE) && !TASK_STARTED) {     
        TASK_STARTED = true;
        TASK_PRESS_COMPLETE = false;
        return UI_ACTION_START_TASK;
    }
    if (point_in_rect(x, y, BUTTON_TAKEORDER))                            return UI_ACTION_TAKE_ORDER;
    if (point_in_rect(x, y, BUTTON_TABLES))                               return UI_ACTION_OPEN_TABLES;
    return UI_ACTION_NONE;
}

static ui_action decode_touch_grid(uint16_t x, uint16_t y) {
    if (point_in_rect(x, y, BUTTON_BACK)) return UI_ACTION_BACK;

    for (int i = 0; i < 6; i++) {
        if (point_in_rect(x, y, TABLE_TILE[i])) {
            return (ui_action)(UI_ACTION_TABLE_TILE_0 + i);
        }
    }
    return UI_ACTION_NONE;
}


/* ------------ Button callbacks ------------ */

void ui_task(void *arg) {
    display_spi_ctx display = *(display_spi_ctx *)arg;
    task_id prev_task_id = {UINT16_MAX, UINT16_MAX};

    while (1) {
        ui_update_snapshot_from_system();
        // Copy the snapshot once per press to avoid mixed fields
        ui_snapshot snap = UI_SNAPSHOT;

        if (UI_MODE == UI_MODE_MAIN && !task_id_equal(snap.task_id, prev_task_id)) {
            if (snap.task_kind == MONITOR_TABLE) {
                draw_button_close_table(display.dev_handle);
            }
            else {
                draw_button_ignore(display.dev_handle);
            }
            draw_active_task_label(display.dev_handle, snap);
            prev_task_id = snap.task_id;
        }

        uint16_t x = 0, y = 0;
        bool pressed = read_touch_point(&x, &y);

        if (pressed && !last_touch_pressed) {
            time_ms now = get_time();

            ui_action act = (UI_MODE == UI_MODE_MAIN) ? decode_touch_main(x, y, snap.task_kind)
                                                     : decode_touch_grid(x, y);

            switch (UI_MODE) {
                case UI_MODE_MAIN: {
                    switch (act) {
                        case UI_ACTION_OPEN_TABLES:
                            UI_MODE = UI_MODE_TABLE_GRID;
                            ui_draw_layout(display.dev_handle);
                            break;

                        case UI_ACTION_IGNORE:
                            if (snap.has_task) {
                                system_apply_user_action_to_task(snap.task_id, USER_ACTION_IGNORE, now);
                            }
                            break;

                        case UI_ACTION_CLOSE_TABLE:
                            if (snap.has_task) {
                                system_apply_user_action_to_task(snap.task_id, USER_ACTION_CLOSE_TABLE, now);
                            }
                            break;

                        case UI_ACTION_START_TASK:
                            if (snap.has_task) {
                                system_apply_user_action_to_task(snap.task_id, USER_ACTION_START_TASK, now);
                            }
                            draw_button_complete(display.dev_handle);
                            break;

                        case UI_ACTION_COMPLETE:
                            if (snap.has_task) {
                                system_apply_user_action_to_task(snap.task_id, USER_ACTION_COMPLETE, now);
                            }
                            // draw_button_start(display.dev_handle);
                            break;

                        case UI_ACTION_TAKE_ORDER:
                                system_take_order_now(snap.table_number, now);
                            break;

                        default:
                            break;
                    }
                } break;

                case UI_MODE_TABLE_GRID: {
                    if (act == UI_ACTION_BACK) {
                        UI_MODE = UI_MODE_MAIN;
                        ui_draw_layout(display.dev_handle);
                        break;
                    }

                    if (act >= UI_ACTION_TABLE_TILE_0 && act <= UI_ACTION_TABLE_TILE_5) {
                        uint8_t table = (uint8_t)(act - UI_ACTION_TABLE_TILE_0); // 0..5

                        // Only start if idle (your requirement)
                        table_state st = system_get_table_state(table);
                        if (st == TABLE_IDLE) {
                            system_apply_table_fsm_event(table, EVENT_CUSTOMERS_SEATED, now);
                            // close the grid immediately
                            UI_MODE = UI_MODE_MAIN;
                            ui_draw_layout(display.dev_handle);
                        } else {
                            // do nothing for in-progress tables
                        }
                    }
                } break;

                default:
                    break;
            }

            ESP_LOGI(TAG_UI, "touch x=%u y=%u mode=%d act=%d",
                     (unsigned)x, (unsigned)y, (int)UI_MODE, (int)act);
        }

        last_touch_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
