#include "esp_log.h"

#include "../include/user_interface.h"
#include "../include/trace_system.h"
#include "../include/table_fsm.h"
#include "../include/trace_scheduler.h"
#include "../include/types.h"
#include "../include/display_util.h"
#include "../include/touch_controller_util.h"
#include "driver/spi_master.h"


/* UI mode: main notifications vs table grid overlay */
typedef enum { UI_MODE_MAIN = 0, UI_MODE_TABLE_GRID = 1 } ui_mode;
static ui_mode UI_MODE = UI_MODE_TABLE_GRID;

static const char *TAG_UI = "ui";
/* Simple edge detector to avoid repeated triggers */
static bool last_touch_pressed = false;
/* Used to store the last drawn UI information */
static ui_snapshot UI_SNAPSHOT;
/* Redraw flag (set when something changes) */
static volatile bool REDRAW_REQUESTED = true;


/* Simple rectangular hit region */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} rect;


/* Button layout (screen-space coordinates) */
static const rect BUTTON_IGNORE         = { .x = 10,  .y = 30,  .w = 100, .h = 60 };
// static const rect BUTTON_CLOSE_TABLE    = { .x = 10,  .y = 30,  .w = 100, .h = 60 };
static const rect BUTTON_COMPLETE       = { .x = 130, .y = 30,  .w = 100, .h = 60 };
static const rect BUTTON_TAKEORDER      = { .x = 10,  .y = 120, .w = 220, .h = 60 };

/* Top bar button to open table grid */
static const rect BUTTON_TABLES   = { .x=10,  .y=245,   .w=220, .h=30 };

/* GRID screen: 6 table tiles (2 cols x 3 rows) */
static const rect TABLE_TILE[6] = {
    { .x=10,  .y=35,  .w=100, .h=60 }, { .x=130, .y=35,  .w=100, .h=60 },
    { .x=10,  .y=105, .w=100, .h=60 }, { .x=130, .y=105, .w=100, .h=60 },
    { .x=10,  .y=175, .w=100, .h=60 }, { .x=130, .y=175, .w=100, .h=60 }
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


static void draw_label(spi_device_handle_t display, rect r, const char *label, bool transparent_bg, uint8_t scale) {
    uint16_t text_x = r.x + 6;
    uint16_t text_y = r.y + (r.h - 7) / 2;

    draw_text(display, text_x, text_y, label, 0xFFFF, 0x0000, transparent_bg, scale);
}


table_state system_get_table_state(uint8_t table_index) {
    if (table_index >= MAX_TABLES) return TABLE_IDLE; // safe fallback
    return table_fsm_instances[table_index].state;
}


/* ------------ Draw functions ------------ */
/* Draw a filled rectangle using line-by-line writes */
static void draw_filled_rect(spi_device_handle_t display,
    uint16_t x, uint16_t y,
    uint16_t width, uint16_t height,
    uint16_t rgb565) {
    /* Scanline buffer sized for max UI element width */
    static uint16_t scanline_buffer[240];
    if (width > (sizeof(scanline_buffer) / sizeof(scanline_buffer[0]))) return;

    for (uint16_t i = 0; i < width; i++) {
        scanline_buffer[i] = rgb565;
    }

    for (uint16_t row = 0; row < height; row++) {
        display_write(display, x, (uint16_t)(y + row), width, 1, scanline_buffer);
    }
}


static void ui_draw_main(spi_device_handle_t display) {
    const uint16_t BG            = 0x0000;
    const uint16_t COLOR_IGNORE  = 0xF800;
    const uint16_t COLOR_COMP    = 0x07E0;
    const uint16_t COLOR_TAKE    = 0xFFE0;
    const uint16_t COLOR_TOPBAR  = 0x39E7; // grey-ish
    const uint8_t scale          = 2;

    display_fill(display, BG);

    // top bar
    draw_filled_rect(display, BUTTON_TABLES.x, BUTTON_TABLES.y, BUTTON_TABLES.w, BUTTON_TABLES.h, COLOR_TOPBAR);
    draw_label(display, BUTTON_TABLES, "TABLES", true, scale);

    // buttons
    draw_filled_rect(display, BUTTON_IGNORE.x, BUTTON_IGNORE.y, BUTTON_IGNORE.w, BUTTON_IGNORE.h, COLOR_IGNORE);
    draw_label(display, BUTTON_IGNORE, "IGNORE", true, scale);

    draw_filled_rect(display, BUTTON_COMPLETE.x, BUTTON_COMPLETE.y, BUTTON_COMPLETE.w, BUTTON_COMPLETE.h, COLOR_COMP);
    draw_label(display, BUTTON_COMPLETE, "COMPLETE", true, scale);

    draw_filled_rect(display, BUTTON_TAKEORDER.x, BUTTON_TAKEORDER.y, BUTTON_TAKEORDER.w, BUTTON_TAKEORDER.h, COLOR_TAKE);
    draw_label(display, BUTTON_TAKEORDER, "TAKE ORDER", true, scale);
}


static void ui_draw_grid(spi_device_handle_t display) {
    const uint16_t BG           = 0x0000;
    const uint16_t COLOR_TILE   = 0x7BEF; // light grey
    const uint16_t COLOR_BACK   = 0x39E7; // grey
    const uint8_t scale         = 2;

    display_fill(display, BG);

    draw_label(display, (rect){.x=10,.y=0,.w=240,.h=30}, "SELECT TABLE", true, scale);

    for (int i = 0; i < 6; i++) {
        draw_filled_rect(display, TABLE_TILE[i].x, TABLE_TILE[i].y, TABLE_TILE[i].w, TABLE_TILE[i].h, COLOR_TILE);
        
        char table_label[32];
        snprintf(table_label, sizeof(table_label), "Table %d", i + 1);
        draw_label(display, TABLE_TILE[i], table_label, true, scale);
    }

    draw_filled_rect(display, BUTTON_BACK.x, BUTTON_BACK.y, BUTTON_BACK.w, BUTTON_BACK.h, COLOR_BACK);
    draw_label(display, BUTTON_BACK, "BACK", true, scale);
}


/* API */

void ui_draw_layout(spi_device_handle_t display) {
    if (UI_MODE == UI_MODE_MAIN) {
        ui_update_snapshot_from_system();
        ui_draw_main(display);
    } else {
        ui_draw_grid(display);
    }
}


ui_action decode_touch_main(uint16_t x, uint16_t y) {
    if (point_in_rect(x, y, BUTTON_IGNORE))                      return UI_ACTION_IGNORE;
    if (point_in_rect(x, y, BUTTON_COMPLETE))                    return UI_ACTION_COMPLETE;
    if (point_in_rect(x, y, BUTTON_TAKEORDER))                   return UI_ACTION_TAKE_ORDER;
    if (point_in_rect(x, y, BUTTON_TABLES))                      return UI_ACTION_OPEN_TABLES;
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

void ui_touch_task(void *arg) {
    display_spi_ctx display = *(display_spi_ctx *)arg;

    while (1) {
        uint16_t x = 0, y = 0;
        bool pressed = read_touch_point(&x, &y);

        if (pressed && !last_touch_pressed) {
            time_ms now = get_time();

            // Copy the snapshot once per press to avoid mixed fields
            ui_snapshot snap = UI_SNAPSHOT;

            ui_action act = (UI_MODE == UI_MODE_MAIN) ? decode_touch_main(x, y)
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
                                UI_MODE = UI_MODE_MAIN;
                                ui_draw_layout(display.dev_handle);
                            }
                            break;

                        case UI_ACTION_COMPLETE:
                            if (snap.has_task) {
                                system_apply_user_action_to_task(snap.task_id, USER_ACTION_COMPLETE, now);
                                UI_MODE = UI_MODE_MAIN;
                                ui_draw_layout(display.dev_handle);
                            }
                            break;

                        case UI_ACTION_TAKE_ORDER:
                                system_take_order_now(snap.table_number, now);
                                UI_MODE = UI_MODE_MAIN;
                                ui_draw_layout(display.dev_handle);
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
