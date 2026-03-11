#include "../include/haptic_driver.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"

#define TAG "DRV2605L"

// ---- I2C config ----
#define I2C_PORT            I2C_NUM_1
#define I2C_SDA_GPIO        17
#define I2C_SCL_GPIO        18
#define I2C_FREQ_HZ         100000

// ---- DRV2605L ----
#define DRV2605L_ADDR       0x5A

#define REG_STATUS          0x00
#define REG_MODE            0x01
#define REG_RTP_INPUT       0x02
#define REG_LIB_SEL         0x03
#define REG_WAVESEQ1        0x04
#define REG_WAVESEQ2        0x05
#define REG_GO              0x0C
#define REG_FEEDBACK        0x1A
#define REG_CONTROL3        0x1D

#define MODE_INTERNAL_TRIGGER   0x00
#define MODE_STANDBY            0x40



static esp_err_t drv2650l_i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t drv2605l_write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_PORT, DRV2605L_ADDR, data, sizeof(data), pdMS_TO_TICKS(100));
}

static esp_err_t drv2605l_read_reg(uint8_t reg, uint8_t *value) {
    return i2c_master_write_read_device(I2C_PORT, DRV2605L_ADDR, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

static void drv2605l_scan_basic(void) {
    uint8_t status = 0;
    esp_err_t err = drv2605l_read_reg(REG_STATUS, &status);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "DRV2605L detected, STATUS = 0x%02X", status);
    } else {
        ESP_LOGE(TAG, "DRV2605L not responding: %s", esp_err_to_name(err));
    }
}

esp_err_t drv2605l_init(void) {
    esp_err_t err;

    ESP_ERROR_CHECK(drv2650l_i2c_master_init());
    vTaskDelay(pdMS_TO_TICKS(100));
    drv2605l_scan_basic();

    // Exit standby, internal trigger mode
    err = drv2605l_write_reg(REG_MODE, MODE_INTERNAL_TRIGGER);
    if (err != ESP_OK) return err;

    // Select haptic library
    err = drv2605l_write_reg(REG_LIB_SEL, 0x01);
    if (err != ESP_OK) return err;

    // Waveform sequence:
    // effect 1 in slot 1, then 0 to end sequence
    err = drv2605l_write_reg(REG_WAVESEQ1, 1);
    if (err != ESP_OK) return err;

    err = drv2605l_write_reg(REG_WAVESEQ2, 0);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t drv2605l_play_effect(uint8_t effect_id) {
    esp_err_t err;

    err = drv2605l_write_reg(REG_WAVESEQ1, effect_id);
    if (err != ESP_OK) return err;

    err = drv2605l_write_reg(REG_WAVESEQ2, 0);
    if (err != ESP_OK) return err;

    err = drv2605l_write_reg(REG_GO, 1);
    return err;
}