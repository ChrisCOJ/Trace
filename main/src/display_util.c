#include "../include/display_util.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_system.h"


#define X_START 0
#define Y_START 20


DRAM_ATTR static const lcd_init_cmd init_cmds[15] = {
    /* Memory Data Access Control, MX=MV=1, MY=ML=MH=0, RGB=0 */
    {MADCTL, {0x08}, 1},
    /* Interface Pixel Format, 16bits/pixel for RGB/MCU interface */
    {PIXEL_FORMAT, {0x55}, 1},
    /* Porch Setting */
    {PORCH_CONTROL, {0x0c, 0x0c, 0x00, 0x33, 0x33}, 5},
    /* Gate Control, Vgh=13.65V, Vgl=-10.43V */
    {GATE_CONTROL, {0x45}, 1},
    /* VCOM Setting, VCOM=1.175V */
    {VCOM, {0x2B}, 1},
    /* LCM Control, XOR: BGR, MX, MH */
    {LCM_CONTROL, {0x2C}, 1},
    /* VDV and VRH Command Enable, enable=1 */
    {VDVVRHEN, {0x01, 0xff}, 2},
    /* VRH Set, Vap=4.4+... */
    {VRH, {0x11}, 1},
    /* VDV Set, VDV=0 */
    {VDV, {0x20}, 1},
    /* Frame Rate Control, 60Hz, inversion=0 */
    {FPS_CONTROL, {0x0f}, 1},
    /* Power Control 1, AVDD=6.8V, AVCL=-4.8V, VDDS=2.3V */
    {POWER_CONTROL, {0xA4, 0xA1}, 2},
    /* Positive Voltage Gamma Control */
    {GAMMA_POS, {0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19}, 14},
    /* Negative Voltage Gamma Control */
    {GAMMA_NEG, {0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19}, 14},
    /* Sleep Out */
    {SLEEP_OUT, {0}, 0x00},
    /* Display On */
    {DISP_ON, {0}, 0x00},
};


void send_display_cmd(spi_device_handle_t dev_handle, const uint8_t cmd, bool keep_cs_active) {
    esp_err_t ret;
    spi_transaction_t spi_msg;
    memset(&spi_msg, 0, sizeof(spi_msg));               // Zero out the transaction
    spi_msg.length = 8;                                 // Command is 8 bits
    spi_msg.tx_buffer = &cmd;                           // The data is the cmd itself
    spi_msg.user = (void*)0;                            // D/C needs to be set to 0
    if (keep_cs_active) {
        spi_msg.flags = SPI_TRANS_CS_KEEP_ACTIVE;       // Keep CS active after data transfer
    }
    ret = spi_device_polling_transmit(dev_handle, &spi_msg);
    assert(ret == ESP_OK);
}


void send_display_data(spi_device_handle_t dev_handle, const uint8_t *data, int len) {
    esp_err_t ret;
    spi_transaction_t spi_msg;

    if (len == 0) {
        return;
    }
    memset(&spi_msg, 0, sizeof(spi_msg));               // Zero out the transaction
    spi_msg.length = len * 8;                           // * 8 to get the length in bits
    spi_msg.tx_buffer = data;                          
    spi_msg.user = (void*)1;                            // D/C needs to be set to 1
    ret = spi_device_polling_transmit(dev_handle, &spi_msg);
    assert(ret == ESP_OK);
}


void send_cmd_with_data(spi_device_handle_t dev_handle, uint8_t cmd, const uint8_t *data, int data_len, bool keep_active) {
    ESP_ERROR_CHECK(spi_device_acquire_bus(dev_handle, portMAX_DELAY));

    send_display_cmd(dev_handle, cmd, keep_active);
    send_display_data(dev_handle, data, data_len);

    spi_device_release_bus(dev_handle);
}


// This function is called (in irq context!) just before a transmission starts. It will
// set the D/C line to the value indicated in the user field.
void lcd_spi_pre_transfer_callback(spi_transaction_t *spi_msg) {
    int dc = (int)spi_msg->user;
    gpio_set_level(DATA_COMMAND, dc);
}


/* To send a set of lines we have to send a command, 2 data bytes, another command, 2 more data bytes and another command
 * before sending the line data itself; a total of 6 transactions. (We can't put all of this in just one transaction
 * because the D/C line needs to be toggled in the middle.)
 * This routine queues these commands up as interrupt transactions so they get
 * sent faster (compared to calling spi_device_transmit several times), and at
 * the mean while the lines for next transactions can get calculated.
 */
static void send_lines(spi_device_handle_t spi, int row_index, uint16_t *pixel_block)
{
    esp_err_t ret;

    // 6 transactions:
    // 0: CASET cmd, 1: CASET data(4)
    // 2: RASET cmd, 3: RASET data(4)
    // 4: RAMWR cmd, 5: pixel data block
    static spi_transaction_t trans[6];

    // Initialize the common bits
    for (int i = 0; i < 6; i++) {
        memset(&trans[i], 0, sizeof(trans[i]));
        trans[i].flags = SPI_TRANS_USE_TXDATA;

        if ((i & 1) == 0) {
            // Command transactions
            trans[i].length = 8;
            trans[i].user   = (void*)0;   // D/C = 0
        } else {
            // Data transactions (we use 4 bytes for CASET/RASET)
            trans[i].length = 8 * 4;
            trans[i].user   = (void*)1;   // D/C = 1
        }
    }

    for (int i = 0; i < 5; i++) {
        trans[i].flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }

    // Compute the address window for this block
    const uint16_t col_start = X_START;
    const uint16_t col_end   = X_START + DISPLAY_WIDTH - 1;

    const uint16_t row_start = Y_START + row_index;
    const uint16_t row_end   = Y_START + row_index + PARALLEL_SPI_LINES - 1;

    // CASET (0x2A): column start/end
    trans[0].tx_data[0] = 0x2A;
    trans[1].tx_data[0] = col_start >> 8;
    trans[1].tx_data[1] = col_start & 0xFF;
    trans[1].tx_data[2] = col_end >> 8;
    trans[1].tx_data[3] = col_end & 0xFF;

    // RASET (0x2B): row start/end
    trans[2].tx_data[0] = 0x2B;
    trans[3].tx_data[0] = row_start >> 8;
    trans[3].tx_data[1] = row_start & 0xFF;
    trans[3].tx_data[2] = row_end   >> 8;
    trans[3].tx_data[3] = row_end   & 0xFF;

    // RAMWR (0x2C): memory write
    trans[4].tx_data[0] = 0x2C;

    // Pixel data block (must contain DISPLAY_WIDTH * PARALLEL_SPI_LINES pixels)
    trans[5].tx_buffer = pixel_block;
    trans[5].length    = DISPLAY_WIDTH * PARALLEL_SPI_LINES * sizeof(uint16_t) * 8;
    trans[5].user      = (void*)1;     // D/C = 1 for data
    trans[5].flags     = 0;            // NOT using TXDATA here

    // Queue all transactions
    for (int i = 0; i < 6; i++) {
        ret = spi_device_queue_trans(spi, &trans[i], portMAX_DELAY);
        assert(ret == ESP_OK);
    }
}


static void send_line_finish(spi_device_handle_t spi) {
    spi_transaction_t *rtrans;
    esp_err_t ret;
    // Wait for all 6 transactions to be done and get back the results.
    for (int x = 0; x < 6; x++) {
        ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
        assert(ret == ESP_OK);
    }
}


void display_fill_white(spi_device_handle_t dev_handle) {
    static uint16_t line[DISPLAY_WIDTH * PARALLEL_SPI_LINES];

    // Prepare one white line
    for (int i = 0; i < DISPLAY_WIDTH * PARALLEL_SPI_LINES; i++) {
        line[i] = 0xFFFF;
    }

    ESP_ERROR_CHECK(spi_device_acquire_bus(dev_handle, portMAX_DELAY));
    // Fill with white lines
    for (int y = 0; y < DISPLAY_HEIGHT; y+= PARALLEL_SPI_LINES) {
        send_lines(dev_handle, y, line);
        send_line_finish(dev_handle);
    }
    spi_device_release_bus(dev_handle);
}


display_spi_ctx display_init() {
    spi_device_handle_t display_spi_handle;
    spi_bus_config_t spi_bus_config = {
        .mosi_io_num = SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PARALLEL_SPI_LINES * DISPLAY_WIDTH * 2 + 8
    };

    spi_device_interface_config_t display_spi_config = {
        .clock_speed_hz = 8 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = CHIP_SELECT,
        .queue_size = 7,                        // We want to be able to queue 7 transactions at a time
        .pre_cb = lcd_spi_pre_transfer_callback,
    };

    // Initialize the SPI bus
    esp_err_t ret = spi_bus_initialize(LCD_HOST, &spi_bus_config, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    // Attach the LCD to the SPI bus
    ret = spi_bus_add_device(LCD_HOST, &display_spi_config, &display_spi_handle);
    ESP_ERROR_CHECK(ret);

    // Initialize non-SPI GPIOs
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = ((1ULL << DATA_COMMAND) | (1ULL << SPI_RST) | (1ULL << BACKLIGHT));
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // Reset display
    gpio_set_level(SPI_RST, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(SPI_RST, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);


    // Send init commands
    for (int i = 0; i < sizeof(init_cmds) / sizeof(lcd_init_cmd); ++i) {
        send_cmd_with_data(display_spi_handle, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_len, false);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    ///Enable backlight
    gpio_set_level(BACKLIGHT, LCD_BACKLIGHT_ON_LEVEL);
    
    display_spi_ctx ctx = {
        .dev_handle = display_spi_handle,
        .ret_code = 0
    };
    return ctx;
}

