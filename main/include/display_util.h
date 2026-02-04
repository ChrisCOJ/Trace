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
#define INVON                           0x21



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


/**
 * Initialise the SPI display interface and ST7789V2 controller.
 *
 * Configures the SPI bus, attaches the display device, initialises GPIOs,
 * performs a hardware reset, and executes the full ST7789V2 initialisation
 * command sequence. The display backlight is enabled before returning.
 *
 * Timing / blocking behaviour:
 *  - Blocks during SPI bus initialisation and device attachment.
 *  - Performs multiple RTOS delays during reset and command sequencing
 *    (hundreds of milliseconds total).
 *  - Must not be called from a time-critical context.
 *
 * This function is intended to be called once during system startup.
 *
 * @return Display SPI context containing the device handle.
 */
display_spi_ctx display_init(void);


/**
 * Fill the entire display with a single RGB565 colour.
 *
 * Clears the display by repeatedly writing horizontal pixel bands using
 * synchronous SPI transactions.
 *
 * Timing / blocking behaviour:
 *  - Blocks for the duration of the full-screen transfer.
 *  - Acquires the SPI bus for the entire operation.
 *  - Performs no RTOS delays but may take several milliseconds depending
 *    on SPI clock rate and display size.
 *
 * @param dev_handle SPI device handle for the display.
 * @param colour RGB565 colour value to fill the screen with.
 */
void display_fill(spi_device_handle_t dev_handle, uint16_t colour);


/**
 * Write an RGB565 pixel block to a rectangular region of the display.
 *
 * Sets the display address window and writes pixel data in RGB565 format.
 * Pixel data is byte-swapped internally to account for little-endian CPU
 * representation.
 *
 * Timing / blocking behaviour:
 *  - Blocks while acquiring the SPI bus and transmitting pixel data.
 *  - Performs no RTOS delays.
 *  - Runtime proportional to the number of pixels written.
 *
 * The caller must ensure that the pixel buffer remains valid for the
 * duration of the call.
 *
 * @param dev_handle SPI device handle for the display.
 * @param x X coordinate of the top-left corner (screen space).
 * @param y Y coordinate of the top-left corner (screen space).
 * @param w Width of the region in pixels.
 * @param h Height of the region in pixels.
 * @param pixels Pointer to RGB565 pixel data.
 */
void display_write(spi_device_handle_t dev_handle, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels);


void draw_text(spi_device_handle_t display, uint16_t x, uint16_t y, const char *text, 
               uint16_t fg, uint16_t bg, bool transparent_bg, uint8_t scale);


#endif