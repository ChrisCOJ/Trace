#include "../include/display_util.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_system.h"
#include "esp_log.h"
#include "../include/font5x7.h"


#define X_START 0
#define Y_START 20


static const char *TAG_DISPLAY = "display";


/* ST7789V2 init sequence */
DRAM_ATTR static const lcd_init_cmd init_cmds[16] = {
    { MADCTL,        {0x00},                                                    1  }, /* MADCTL: orientation/BGR */
    { PIXEL_FORMAT,  {0x55},                                                    1  }, /* 16bpp RGB565 */
    { PORCH_CONTROL, {0x0c, 0x0c, 0x00, 0x33, 0x33},                            5  }, /* porch */
    { GATE_CONTROL,  {0x45},                                                    1  }, /* gate */
    { VCOM,          {0x2B},                                                    1  }, /* VCOM */
    { LCM_CONTROL,   {0x2C},                                                    1  }, /* LCM control */
    { VDVVRHEN,      {0x01, 0xff},                                              2  }, /* enable VDV/VRH */
    { VRH,           {0x11},                                                    1  }, /* VRH */
    { VDV,           {0x20},                                                    1  }, /* VDV */
    { FPS_CONTROL,   {0x0f},                                                    1  }, /* frame rate */
    { POWER_CONTROL, {0xA4, 0xA1},                                              2  }, /* power */
    { GAMMA_POS,     {0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47,
                      0x09, 0x15, 0x12, 0x16, 0x19},                           14 },  /* gamma + */
    { GAMMA_NEG,     {0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40,
                      0x0E, 0x1C, 0x18, 0x16, 0x19},                           14 },  /* gamma - */
    { SLEEP_OUT,     {0},                                                       0  }, /* sleep out */
    { DISP_ON,       {0},                                                       0  }, /* display on */
    { INVON,         {0},                                                       0 },  /* Invert colours */
};


/* SPI D/C is driven via pre-transfer callback using transaction->user */
static void send_display_cmd(spi_device_handle_t dev_handle, const uint8_t cmd, bool keep_cs_active) {
    spi_transaction_t transaction;
    memset(&transaction, 0, sizeof(transaction));

    transaction.length    = 8;
    transaction.tx_buffer = &cmd;
    transaction.user      = (void*)0;

    if (keep_cs_active) {
        transaction.flags = SPI_TRANS_CS_KEEP_ACTIVE;
    }

    esp_err_t result = spi_device_polling_transmit(dev_handle, &transaction);
    assert(result == ESP_OK);
}


static void send_display_data(spi_device_handle_t dev_handle, const uint8_t *data, int data_length) {
    if (data_length == 0) return;

    spi_transaction_t transaction;
    memset(&transaction, 0, sizeof(transaction));

    transaction.length    = data_length * 8;
    transaction.tx_buffer = data;
    transaction.user      = (void*)1;

    esp_err_t result = spi_device_polling_transmit(dev_handle, &transaction);
    assert(result == ESP_OK);
}


/* Convenience wrapper for cmd + payload */
static void send_cmd_with_data(spi_device_handle_t dev_handle,
                        uint8_t cmd,
                        const uint8_t *data,
                        int data_len,
                        bool keep_active) {
    ESP_ERROR_CHECK(spi_device_acquire_bus(dev_handle, portMAX_DELAY));

    send_display_cmd(dev_handle, cmd, keep_active);
    send_display_data(dev_handle, data, data_len);

    spi_device_release_bus(dev_handle);
}


/* SPI pre-callback: flip D/C for cmd vs data */
static void lcd_spi_pre_transfer_callback(spi_transaction_t *transaction) {
    int dc_level = (int)transaction->user;
    gpio_set_level(DATA_COMMAND, dc_level);
}


/* Queue CASET/RASET/RAMWR + pixel block for a band of lines */
static void send_lines(spi_device_handle_t spi_handle,
                       int row_index,
                       uint16_t *pixel_block) {
    static spi_transaction_t transactions[6];

    for (int transaction_index = 0; transaction_index < 6; transaction_index++) {
        memset(&transactions[transaction_index], 0, sizeof(transactions[transaction_index]));
        transactions[transaction_index].flags = SPI_TRANS_USE_TXDATA;

        const bool is_command = ((transaction_index & 1) == 0);
        if (is_command) {
            transactions[transaction_index].length = 8;
            transactions[transaction_index].user   = (void*)0;
        } else {
            transactions[transaction_index].length = 8 * 4;
            transactions[transaction_index].user   = (void*)1;
        }
    }

    for (int i = 0; i < 5; i++) {
        transactions[i].flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }

    const uint16_t column_start = X_START;
    const uint16_t column_end   = X_START + DISPLAY_WIDTH - 1;

    const uint16_t row_start = Y_START + row_index;
    const uint16_t row_end   = Y_START + row_index + PARALLEL_SPI_LINES - 1;

    transactions[0].tx_data[0] = 0x2A;
    transactions[1].tx_data[0] = column_start >> 8;
    transactions[1].tx_data[1] = column_start & 0xFF;
    transactions[1].tx_data[2] = column_end >> 8;
    transactions[1].tx_data[3] = column_end & 0xFF;

    transactions[2].tx_data[0] = 0x2B;
    transactions[3].tx_data[0] = row_start >> 8;
    transactions[3].tx_data[1] = row_start & 0xFF;
    transactions[3].tx_data[2] = row_end >> 8;
    transactions[3].tx_data[3] = row_end & 0xFF;

    transactions[4].tx_data[0] = 0x2C;

    transactions[5].tx_buffer = pixel_block;
    transactions[5].length    = DISPLAY_WIDTH * PARALLEL_SPI_LINES * sizeof(uint16_t) * 8;
    transactions[5].user      = (void*)1;
    transactions[5].flags     = 0;

    for (int transaction_index = 0; transaction_index < 6; transaction_index++) {
        esp_err_t result = spi_device_queue_trans(spi_handle,
                                                  &transactions[transaction_index],
                                                  portMAX_DELAY);
        assert(result == ESP_OK);
    }
}


/* Block until the queued band has fully completed */
static void send_line_finish(spi_device_handle_t spi_handle)
{
    spi_transaction_t *returned_transaction = NULL;

    for (int completed = 0; completed < 6; completed++) {
        esp_err_t result = spi_device_get_trans_result(spi_handle,
                                                       &returned_transaction,
                                                       portMAX_DELAY);
        assert(result == ESP_OK);
    }
}


/* ------------------- Text Render ------------------- */
/* Draw a solid block (scale x scale) */
static inline void draw_block(spi_device_handle_t display, uint16_t x, uint16_t y, uint8_t scale, uint16_t color) {
    /* small stack buffer */
    uint16_t line[16];
    if (scale == 0 || scale > 4) return;

    for (uint8_t i = 0; i < scale; i++) line[i] = color;

    for (uint8_t row = 0; row < scale; row++) {
        display_write(display, x, (uint16_t)(y + row), scale, 1, line);
    }
}


static void draw_char(spi_device_handle_t display, uint16_t x, uint16_t y, char c, 
                      uint16_t color, uint8_t scale) {
    if (scale == 0) return;

    const uint8_t *glyph = get_glyph(c);
    if (!glyph) return;

    // 5 columns × 7 rows
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];

        for (uint8_t row = 0; row < 7; row++) {
            bool char_color = (bits & (1u << row)) != 0;

        if (!char_color) {
            continue;
        }

            // scaled pixel = filled block
            draw_block(display,
                       (uint16_t)(x + col * scale),
                       (uint16_t)(y + row * scale),
                       scale,
                       color);
        }
    }
}


void draw_text(spi_device_handle_t display, uint16_t x, uint16_t y, const char *text, 
               uint16_t color, uint8_t scale) {
    if (!text || scale == 0) return;

    uint16_t cx = x;
    const uint16_t advance = (uint16_t)(CHAR_WIDTH * scale); // 5 cols + 1 space

    while (*text) {
        draw_char(display, cx, y, *text, color, scale);
        cx = (uint16_t)(cx + advance);
        text++;
    }
}




/* Full-screen clear using band writes */
void display_fill(spi_device_handle_t dev_handle, uint16_t colour) {
    static uint16_t band_buffer[DISPLAY_WIDTH * PARALLEL_SPI_LINES];

    for (int pixel_index = 0; pixel_index < DISPLAY_WIDTH * PARALLEL_SPI_LINES; pixel_index++) {
        band_buffer[pixel_index] = colour;
    }

    ESP_ERROR_CHECK(spi_device_acquire_bus(dev_handle, portMAX_DELAY));

    for (int y = 0; y < DISPLAY_HEIGHT; y += PARALLEL_SPI_LINES) {
        send_lines(dev_handle, y, band_buffer);
        send_line_finish(dev_handle);
    }

    spi_device_release_bus(dev_handle);
}


/* SPI init + ST7789 init sequence */
display_spi_ctx display_init(void) {
    spi_device_handle_t display_spi_handle = NULL;

    spi_bus_config_t bus_config = {
        .mosi_io_num     = SPI_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = PARALLEL_SPI_LINES * DISPLAY_WIDTH * 2 + 8
    };

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = 8 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num    = CHIP_SELECT,
        .queue_size      = 7,
        .pre_cb          = lcd_spi_pre_transfer_callback,
    };

    esp_err_t result = spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(result);

    result = spi_bus_add_device(LCD_HOST, &device_config, &display_spi_handle);
    ESP_ERROR_CHECK(result);

    gpio_config_t gpio_output_config = {0};
    gpio_output_config.pin_bit_mask = ((1ULL << DATA_COMMAND) |
                                       (1ULL << SPI_RST) |
                                       (1ULL << BACKLIGHT));
    gpio_output_config.mode         = GPIO_MODE_OUTPUT;
    gpio_output_config.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&gpio_output_config);

    gpio_set_level(SPI_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(SPI_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    for (int init_index = 0; init_index < (int)(sizeof(init_cmds) / sizeof(init_cmds[0])); init_index++) {
        send_cmd_with_data(display_spi_handle,
                           init_cmds[init_index].cmd,
                           init_cmds[init_index].data,
                           init_cmds[init_index].data_len,
                           false);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    gpio_set_level(BACKLIGHT, LCD_BACKLIGHT_ON_LEVEL);

    display_spi_ctx ctx = {
        .dev_handle = display_spi_handle,
        .ret_code   = 0
    };
    return ctx;
}


/* Write an RGB565 block into an address window */
void display_write(spi_device_handle_t dev_handle,
                   uint16_t x, uint16_t y,
                   uint16_t w, uint16_t h,
                   const uint16_t *pixels)
{
    if (!pixels || w == 0 || h == 0) return;

    const uint16_t x0 = x + X_START;
    const uint16_t y0 = y + Y_START;
    const uint16_t x1 = x0 + w - 1;
    const uint16_t y1 = y0 + h - 1;

    const uint8_t caset_payload[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    const uint8_t raset_payload[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };

    ESP_ERROR_CHECK(spi_device_acquire_bus(dev_handle, portMAX_DELAY));

    send_display_cmd(dev_handle, 0x2A, true);
    send_display_data(dev_handle, caset_payload, sizeof(caset_payload));

    send_display_cmd(dev_handle, 0x2B, true);
    send_display_data(dev_handle, raset_payload, sizeof(raset_payload));

    send_display_cmd(dev_handle, 0x2C, true);

    /* RGB565 needs byte swap on little-endian CPU */
    const size_t total_pixels = (size_t)w * (size_t)h;

    static uint16_t swap_buffer[240];
    size_t pixels_sent = 0;

    while (pixels_sent < total_pixels) {
        size_t chunk_pixels = total_pixels - pixels_sent;
        const size_t swap_capacity = sizeof(swap_buffer) / sizeof(swap_buffer[0]);

        if (chunk_pixels > swap_capacity) chunk_pixels = swap_capacity;

        for (size_t i = 0; i < chunk_pixels; i++) {
            uint16_t pixel = pixels[pixels_sent + i];
            swap_buffer[i] = (uint16_t)((pixel << 8) | (pixel >> 8));
        }

        send_display_data(dev_handle,
                          (const uint8_t*)swap_buffer,
                          (int)(chunk_pixels * sizeof(uint16_t)));

        pixels_sent += chunk_pixels;
    }

    spi_device_release_bus(dev_handle);
}