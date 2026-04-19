#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gyro_task.h"
#include "config.h"
#include "i2c_bus.h"
#include "telemetry.h"
#include "bno055.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "gyro_task";

#define BNO055_ESP32S3_MAX_SCL_HZ 100000u

static bno055_t s_imu;

esp_err_t gyro_peripherals_init(void)
{
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t scl_hz = (uint32_t)CFG_BNO055_I2C_FREQ_KHZ * 1000u;
    if (scl_hz > BNO055_ESP32S3_MAX_SCL_HZ) {
        scl_hz = BNO055_ESP32S3_MAX_SCL_HZ;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CFG_BNO055_I2C_ADDR,
        .flags.disable_ack_check = 0,
        .scl_speed_hz = scl_hz,
        .scl_wait_us = 0xffff,
    };

    esp_err_t err = i2c_master_bus_add_device(i2c_bus_get_handle(), &dev_cfg, &s_imu.config.slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add BNO055 device failed: %s", esp_err_to_name(err));
        xSemaphoreGive(i2c_mutex);
        return err;
    }

    err = bno055_initialize(&s_imu);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bno055_initialize failed: %s", esp_err_to_name(err));
        xSemaphoreGive(i2c_mutex);
        return err;
    }

    err = bno055_configure(&s_imu, NDOF_MODE, (ACC_MG | GY_RPS | EUL_DEG));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bno055_configure failed: %s", esp_err_to_name(err));
        xSemaphoreGive(i2c_mutex);
        return err;
    }

    xSemaphoreGive(i2c_mutex);
    ESP_LOGI(TAG, "BNO055 ready (NDOF)");
    return ESP_OK;
}

void gyro_task(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t period = pdMS_TO_TICKS(200);

    while (1) {
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(period);
            continue;
        }

        esp_err_t err = bno055_get_readings(&s_imu, EULER_ANGLE);
        if (err == ESP_OK) {
            telemetry_set_imu(s_imu.euler_angle.yaw,
                              s_imu.euler_angle.roll,
                              s_imu.euler_angle.pitch,
                              true);
        } else {
            telemetry_set_imu(0, 0, 0, false);
        }

        xSemaphoreGive(i2c_mutex);

        vTaskDelay(period);
    }
}
