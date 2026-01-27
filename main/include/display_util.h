#ifndef display_util_h
#define display_util_h

#include <stdbool.h>
#include <stdint.h>
#include "driver/spi_master.h"


#define DISPLAY_WIDTH                   240
#define DISPLAY_HEIGHT                  280
#define LCD_HOST                        SPI2_HOST
#define LCD_BACKLIGHT_ON_LEVEL          1
 
#define DATA_COMMAND                    4
#define CHIP_SELECT                     5
#define SCLK                            6
#define SPI_MOSI                        7
#define SPI_RST                         8  
#define PARALLEL_SPI_LINES              20
#define BACKLIGHT                       15

// ST7789V2 commands:
#define SWRESET                         0x01
#define SLEEP_OUT                       0x11
#define DISP_ON                         0x29
#define LCM_CONTROL                     0xC0
#define FPS_CONTROL                     0xC6
#define PIXEL_FORMAT                    0x3A
#define MADCTL                          0x36
#define PORCH_CONTROL                   0xB2
#define GATE_CONTROL                    0xB7
#define VCOM                            0xBB
#define POWER_CONTROL                   0xD0
#define GAMMA_POS                       0xE0
#define GAMMA_NEG                       0xE1
#define COL_ADDR                        0x2A
#define ROW_ADDR                        0x2B
#define RAMWR                           0x2C
#define VDV                             0x20
#define VDVVRHEN                        0xC2
#define VRH                             0xC3


typedef struct {
    spi_device_handle_t dev_handle;
    uint8_t ret_code;
} display_spi_ctx;


/*
 The LCD needs a bunch of command/argument values to be initialized. They are stored in this struct.
*/
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t data_len;
} lcd_init_cmd;


void send_display_cmd(spi_device_handle_t dev_handle, const uint8_t cmd, bool keep_cs_active);
void send_display_data(spi_device_handle_t dev_handle, const uint8_t *data, int len);
void send_cmd_with_data(spi_device_handle_t dev_handle, uint8_t cmd, const uint8_t *data, int data_len, bool keep_active);
void lcd_spi_pre_transfer_callback(spi_transaction_t *spi_msg);
static void send_lines(spi_device_handle_t spi, int ypos, uint16_t *linedata);
static void send_line_finish(spi_device_handle_t spi);
void display_fill_white(spi_device_handle_t dev_handle);
display_spi_ctx display_init();



#endif