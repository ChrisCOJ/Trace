#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/mpu_i2c.h"

#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "sdkconfig.h"


/********************************************
 * This file contains mpu6050 functionality 
 *******************************************/


 mpu6050_i2c_context setup_mpu6050_i2c() {
    i2c_master_bus_handle_t i2c_bus_handle;
    i2c_master_dev_handle_t mpu6050_dev_handle;

    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_MASTER_PORT,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));

    i2c_device_config_t mpu6050_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = MPU6050_I2C_ADDR,
        .scl_speed_hz = I2C_SCL_CLK_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &mpu6050_dev_config, &mpu6050_dev_handle));
    
    mpu6050_i2c_context ctx = {
        .bus_handle = i2c_bus_handle,
        .dev_handle = mpu6050_dev_handle
    };
    return ctx;
}
 

esp_err_t mpu_reg_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_address, uint8_t data) {
    // Sends the address of the register to be written to, then sends the actual data to be written.
    uint8_t write_buf[2] = { reg_address, data };
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}



esp_err_t mpu_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg_address, uint8_t *read_buffer, size_t read_buffer_size) {
    // Key takeaway: Reads data from reg_address into read_buffer
    // First writes the address of the register to be read from, then reads data from that register into a user defined buffer

    return i2c_master_transmit_receive(dev_handle, &reg_address, 1, read_buffer, read_buffer_size, 
                                       I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}


esp_err_t mpu_init(i2c_master_dev_handle_t dev_handle, uint8_t accel_accuracy, uint8_t gyro_accuracy) {
    esp_err_t err;
    // Wake up the MPU6050
    err = mpu_reg_write_byte(dev_handle, MPU6050_PWR_MGMT1_REG, MPU6050_WAKE_UP_SIG);
    if (err) return err;
    // Configure the MPU6050 accelerometer
    err = mpu_reg_write_byte(dev_handle, MPU6050_ACCEL_CONFIG_REG, accel_accuracy);
    if (err) return err;

    // Configure the MPU6050 gyroscope
    err = mpu_reg_write_byte(dev_handle, MPU6050_GYRO_CONFIG_REG, gyro_accuracy);
    if (err) return err;

    uint8_t read_buffer[1];
    err = mpu_read_reg(dev_handle, MPU6050_GYRO_CONFIG_REG, read_buffer, 1);
    ESP_LOGI(MPU_TAG, "GYRO CONFIG REG = 0x%02X", read_buffer[0]);
    return err;
}


int mpu_read_data(int data_type, i2c_master_dev_handle_t dev_handle, int16_t *reader_arr, size_t reader_arr_size) {
    esp_err_t err;

    if (reader_arr_size < 3) {  // Error handling, reader array size must be >= 3
        ESP_LOGE(MPU_TAG, "Invalid reader array parameter size. Size must be >= 3");
        return -1;
    }

    // Write the address of the first accelerometer or gyroscope address and read the next 6 bytes into data
    uint8_t data[6];
    switch (data_type) {
        case MPU_ACCEL_DATA:
            err = mpu_read_reg(dev_handle, MPU6050_ACCEL_REG, data, sizeof(data));
            break;

        case MPU_GYRO_DATA:
            err = mpu_read_reg(dev_handle, MPU6050_GYRO_REG, data, sizeof(data));
            break;
        
        default:
            ESP_LOGE(MPU_TAG, "The data_type parameter %d is invalid", data_type);
            return -1;
    }

    if (err != ESP_OK) {
        return err;
    }

    int16_t x = ((data[0] << 8) | data[1]);
    reader_arr[0] = x;
    int16_t y = ((data[2] << 8) | data[3]);
    reader_arr[1] = y;
    int16_t z = ((data[4] << 8) | data[5]);
    reader_arr[2] = z;
    return ESP_OK;
}