#ifndef mpu_i2c_h
#define mpu_i2c_h

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#define I2C_SDA_IO                      22
#define I2C_SCL_IO                      21
#define I2C_MASTER_PORT                 I2C_NUM_0     // Port number used by esp32
#define I2C_SCL_CLK_HZ                  100000        // I2C Clock speed in hz
#define I2C_MASTER_TIMEOUT_MS           1000          // Time in ms to timeout communication

#define MPU6050_I2C_ADDR                0x68          // I2C address of the MPU6050 sensor
#define MPU6050_PWR_MGMT1_REG           0x6B  
#define MPU6050_WAKE_UP_SIG             0x00        
#define MPU6050_RESET_BIT               7
#define MPU6050_SLEEP_BIT               6

#define MPU6050_ACCEL_REG               0x3B          // The address of the first MPU6050's accelerometer register
#define MPU6050_ACCEL_CONFIG_REG        0x1C          // Accel Config register address. Bits 4 and 3 are used to set the maximum value of the accelerometer outputs
#define MPU6050_ACCEL_2G                0x00          // Value to configure the accelerometer sensitivity to be 2g
#define MPU6050_ACCEL_4G                0x08          // Value to configure the accelerometer sensitivity to be 4g
#define MPU6050_ACCEL_8G                0x10          // Value to configure the accelerometer sensitivity to be 8g
#define MPU6050_ACCEL_16G               0x18          // Value to configure the accelerometer sensitivity to be 8g
#define MPU_2G_DIV                      16384.f       // Divide raw binary value by this to get G
#define MPU_4G_DIV                      8192.f        // Divide raw binary value by this to get G
#define MPU_8G_DIV                      4096.f        // Divide raw binary value by this to get G
#define MPU_16G_DIV                     2048.f        // Divide raw binary value by this to get G

#define MPU6050_GYRO_REG                0x43          // The address of the first MPU6050's gyro register
#define MPU6050_GYRO_CONFIG_REG         0x1B          // Gyro Config register address. Bits 4 and 3 are used to set the maximum value of the gyro outputs
#define MPU6050_GYRO_250_DEG            0x00          // Value to configure the gyro sensitivity to be 250 degrees / second
#define MPU6050_GYRO_500_DEG            0x08          // Value to configure the gyro sensitivity to be 500 degrees / second
#define MPU6050_GYRO_1000_DEG           0x10          // Value to configure the gyro sensitivity to be 1000 degrees / second
#define MPU6050_GYRO_2000_DEG           0x18          // Value to configure the gyro sensitivity to be 2000 degrees / second
#define MPU_250_DEG_DIV                 131.f         // Divide raw binary value by this to get deg/sec
#define MPU_500_DEG_DIV                 65.5f         // Divide raw binary value by this to get deg/sec
#define MPU_1000_DEG_DIV                32.8f         // Divide raw binary value by this to get deg/sec
#define MPU_2000_DEG_DIV                16.4f         // Divide raw binary value by this to get deg/sec

#define MAX_MPU_RAW_VALUE               32767       
#define MIN_MPU_RAW_VALUE              -32768

#define MPU_READ_SUCCESS                0             // Success status code for reading data from the MPU
#define MPU_TAG                         "MPU"

/**
 * @enum mpu_data_type
 * @brief Types of data that can be read from the MPU-6050
 */
enum mpu_data_type {
    MPU_ACCEL_DATA = 0,   /**< Accelerometer data type */
    MPU_GYRO_DATA = 1     /**< Gyroscope data type */
};

typedef struct {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    uint8_t ret_code;
} mpu6050_i2c_context;

/**
 * @brief Performs necessary init operations required for i2c transactions (i.e. initliazes master bus, adds device)
 * @return Struct containing resources needed for i2c transaction (i.e. handles)
*/
mpu6050_i2c_context setup_mpu6050_i2c();

/**
 * @brief Reads bytes from a register on the MPU-6050   
 *
 * @param[in] dev_handle I2C device handle of the MPU-6050
 * @param[in] reg_address Register address to read from
 * @param[out] read_buffer Buffer to store read bytes
 * @param[in] read_buffer_size Size of the read buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg_address, uint8_t *read_buffer, size_t read_buffer_size);

/**
 * @brief Writes a single byte to a register on the MPU-6050
 *
 * @param[in] dev_handle I2C device handle of the MPU-6050
 * @param[in] reg_address Register address to write to
 * @param[in] data Data byte to write
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu_reg_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_address, uint8_t data);

/**
 * @brief Initializes the MPU-6050 with specified accelerometer and gyroscope configurations
 *
 * @param[in] dev_handle I2C device handle of the MPU-6050
 * @param[in] accel_accuracy Accelerometer sensitivity setting (MPU6050_ACCEL_2G, _4G, _8G, _16G)
 * @param[in] gyro_accuracy Gyroscope sensitivity setting (MPU6050_GYRO_250_DEG, etc.)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu_init(i2c_master_dev_handle_t dev_handle, uint8_t accel_accuracy, uint8_t gyro_accuracy);

/**
 * @brief Reads accelerometer or gyroscope data into the provided buffer
 *
 * @param[in] data_type Type of IMU data to be read (0 for MPU_ACCEL_DATA or 1 for MPU_GYRO_DATA)
 * @param[in] dev_handle I2C device handle of the MPU-6050
 * @param[out] reader_array Buffer to store the 3-axis data
 * @param[in] reader_array_size Size of the reader_array (expected minimum: 3)
 * @return 0 on success, error code otherwise
 */
int mpu_read_data(int data_type, i2c_master_dev_handle_t dev_handle, int16_t *reader_array, size_t reader_array_size);

#endif