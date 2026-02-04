#include "touch_controller_util.h"

#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

#include "../include/display_util.h" // for DISPLAY_WIDTH / DISPLAY_HEIGHT (or move those to a shared config)


static const char *TAG_TOUCH = "touch";


/* Touch controller (CST816S, Waveshare 1.69") */
#define TP_SCL_GPIO      10
#define TP_SDA_GPIO      11
#define TP_RST_GPIO      15
#define TP_INT_GPIO      16
#define CST816S_I2C_ADDR 0x15

#define TP_I2C_PORT      I2C_NUM_0
#define TP_I2C_FREQ_HZ   400000

static bool initialized = false;


/* Read a contiguous register block from the touch controller */
static esp_err_t touch_i2c_read_register_block(uint8_t start_register,
                                               uint8_t *out_buffer,
                                               size_t buffer_length) {
    return i2c_master_write_read_device(
        TP_I2C_PORT,
        CST816S_I2C_ADDR,
        &start_register, 1,
        out_buffer, buffer_length,
        pdMS_TO_TICKS(50)
    );
}


void touch_init(void)
{
    if (initialized) return;

    /* Touch interrupt line (active low) */
    gpio_config_t touch_interrupt_config = {
        .pin_bit_mask = (1ULL << TP_INT_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&touch_interrupt_config));

    /* Touch reset line */
    gpio_config_t touch_reset_config = {
        .pin_bit_mask = (1ULL << TP_RST_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&touch_reset_config));

    /* I2C master configuration */
    i2c_config_t i2c_master_config = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TP_SDA_GPIO,
        .scl_io_num       = TP_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TP_I2C_FREQ_HZ,
        .clk_flags        = 0
    };
    ESP_ERROR_CHECK(i2c_param_config(TP_I2C_PORT, &i2c_master_config));
    ESP_ERROR_CHECK(i2c_driver_install(TP_I2C_PORT, i2c_master_config.mode, 0, 0, 0));

    /* Hard reset to ensure a known controller state */
    gpio_set_level(TP_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TP_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    initialized = true;
    ESP_LOGI(TAG_TOUCH, "CST816S touch init done");
}


/* Read a single touch point; returns true only when a finger is present */
bool read_touch_point(uint16_t *out_x, uint16_t *out_y) {
    touch_init();
    if (!out_x || !out_y) return false;

    /* CST816S touch data block starting at 0x02 */
    uint8_t touch_data[6] = {0};
    esp_err_t read_result = touch_i2c_read_register_block(0x02, touch_data, sizeof(touch_data));

    /* First byte reports number of active fingers */
    const uint8_t finger_count = touch_data[0];
    if (finger_count == 0) return false;

    /* Coordinates are 12-bit values split across high/low registers */
    const uint16_t touch_x = ((uint16_t)(touch_data[1] & 0x0F) << 8) | touch_data[2];
    const uint16_t touch_y = ((uint16_t)(touch_data[3] & 0x0F) << 8) | touch_data[4];

    if (touch_x >= DISPLAY_WIDTH || touch_y >= DISPLAY_HEIGHT) return false;

    *out_x = touch_x;
    *out_y = touch_y;
    return true;
}
