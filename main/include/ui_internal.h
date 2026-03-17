#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../include/display_util.h"
#include "../include/table_fsm.h"


extern uint8_t UI_GRID_PAGE;


/* ---- Table grid layout constants ----*/
enum {
    NUM_OF_TABLES   = 24,
    TABLES_PER_PAGE = 9
};


/* ---- Layout / dimension constants ---- */
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

    UI_TABLE_INFO_BTN_H = 60,
    UI_TABLE_INFO_TAKE_ORDER_Y = 115,
    UI_TABLE_INFO_TAKE_ORDER_W = UI_HALF_BTN_W,
    UI_TABLE_INFO_BILL_Y = 185,
    UI_TABLE_INFO_BILL_W = UI_HALF_BTN_W,
    UI_TABLE_INFO_BACK_Y = 185,

    UI_TOUCH_PAD_X = 10,
    UI_TOUCH_PAD_Y = 5,

    UI_CORNER_RADIUS = 10,
    UI_TEXT_VERTICAL_SPACING = 7,

    UI_NO_TABLE_SELECTED = 0xFF,

    UI_GRID_NAV_BTN_W = 120,

    UI_TOPBAR_BACK_W = 55,

    UI_SLEEP_CORNER_MIN_X = 195,
    UI_SLEEP_CORNER_MAX_Y = 40,
    UI_SLEEP_HOLD_MS      = 1000,
    UI_INACTIVITY_SLEEP_MS = 30000,

    UI_BATT_X       = 195,
    UI_BATT_Y       = 8,
    UI_BATT_W       = 30,
    UI_BATT_H       = 16,
    UI_BATT_TIP_W   = 3,
    UI_BATT_TIP_H   = 7,
    UI_BATT_BORDER  = 2,
    UI_BATT_BARS    = 4,
};


/* ---- RGB565 colour palette ---- */
enum {
    WHITE       = 0xFFFF,
    BLACK       = 0x0000,

    RED         = 0xF980,
    GREEN       = 0x07E0,
    BLUE        = 0x001F,

    YELLOW      = 0xFEE0,
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
    AQUA        = 0x07FF,
};


/* ---- Font scales ---- */
enum {
    UI_TEXT_SCALE       = 2,
    UI_TITLE_SCALE      = 2,
    UI_SMALL_TEXT_SCALE = 1,
};


typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_IGNORE,
    UI_ACTION_COMPLETE,
    UI_ACTION_START_TASK,
    UI_ACTION_TAKE_ORDER,
    UI_ACTION_BILL,
    UI_ACTION_OPEN_TABLES,
    UI_ACTION_BACK,
    UI_ACTION_TABLE_TILE_1,
    UI_ACTION_TABLE_TILE_2,
    UI_ACTION_TABLE_TILE_3,
    UI_ACTION_TABLE_TILE_4,
    UI_ACTION_TABLE_TILE_5,
    UI_ACTION_TABLE_TILE_6,
    UI_ACTION_TABLE_TILE_7,
    UI_ACTION_TABLE_TILE_8,
    UI_ACTION_TABLE_TILE_9,
    UI_ACTION_TABLE_INFO_TAKE_ORDER,
    UI_ACTION_TABLE_INFO_BILL,
    UI_ACTION_TABLE_INFO_BACK,
    UI_ACTION_TABLE_INFO_UNDO,
    UI_ACTION_GRID_PREV_PAGE,
    UI_ACTION_GRID_NEXT_PAGE,
} ui_action;


typedef struct {
    bool has_task;
    task_id task_id;
    task_kind task_kind;
    uint8_t table_number;
} ui_snapshot;


/* ---- Button style ---- */
/* fill, text, border */
typedef enum {
    BTN_PRIMARY,
    BTN_SECONDARY,
    BTN_DANGER,
    BTN_DISABLED,
} btn_style;


typedef enum {
    UI_TASK_STATE_IDLE = 0,
    UI_TASK_STATE_READY,
    UI_TASK_STATE_IN_PROGRESS,
} ui_task_state;


typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} rect;


/* ---- Colour scheme ---- */
static const uint16_t COLOR_LABEL_PRIMARY   = BLACK;
static const uint16_t COLOR_LABEL_SECONDARY = WHITE;
static const uint16_t COLOR_LABEL_CHROME    = WHITE;

static const uint16_t BG                    = BLACK;

static const uint16_t PRIMARY_ACCENT_COLOR  = GREEN;
static const uint16_t SECONDARY_ACCENT_COLOR = BLACK;
static const uint16_t DANGER_COLOR          = RED;

static const uint16_t BUTTON_BORDER         = GREEN;


/* ---- Button / region layout (screen-space coordinates) ---- */
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
static const rect MAIN_TABLES_BTN = {
    .x = UI_TOPBAR_X,
    .y = UI_TOPBAR_Y,
    .w = UI_TOPBAR_W,
    .h = UI_TOPBAR_H
};
static const rect TABLE_INFO_TAKE_ORDER_BTN = {
    .x = UI_MARGIN_X,
    .y = UI_TABLE_INFO_TAKE_ORDER_Y,
    .w = UI_FULL_BTN_W,
    .h = UI_TABLE_INFO_BTN_H
};
static const rect TABLE_INFO_BILL_BTN = {
    .x = UI_MARGIN_X,
    .y = UI_TABLE_INFO_BILL_Y,
    .w = UI_HALF_BTN_W,
    .h = UI_TABLE_INFO_BTN_H
};
static const rect TOPBAR_BACK_BTN = {
    .x = 0,
    .y = UI_TOPBAR_Y,
    .w = UI_TOPBAR_BACK_W,
    .h = UI_TOPBAR_H
};
static const rect TABLE_INFO_UNDO_BTN = {
    .x = UI_MARGIN_X + UI_HALF_BTN_W + UI_LARGE_GAP,
    .y = UI_TABLE_INFO_BACK_Y,
    .w = UI_HALF_BTN_W,
    .h = UI_TABLE_INFO_BTN_H
};
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

#endif /* ui_internal_h */
