#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fuel_gauge_task.h"
#include "bq27441.h"
#include "config.h"
#include "i2c_bus.h"
#include "telemetry.h"
#include "esp_log.h"

static const char *TAG = "fuel_gauge";

#define BQ27441_MAX_SCL_HZ 100000u

static i2c_master_dev_handle_t s_fg_dev;

esp_err_t fuel_gauge_peripherals_init(void)
{
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t scl_hz = (uint32_t)CFG_BNO055_I2C_FREQ_KHZ * 1000u;
    if (scl_hz > BQ27441_MAX_SCL_HZ) {
        scl_hz = BQ27441_MAX_SCL_HZ;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CFG_BQ27441_I2C_ADDR,
        .flags.disable_ack_check = 0,
        .scl_speed_hz = scl_hz,
        .scl_wait_us = 0xffff,
    };

    esp_err_t err = i2c_master_bus_add_device(i2c_bus_get_handle(), &dev_cfg, &s_fg_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add BQ27441 device failed: %s", esp_err_to_name(err));
        xSemaphoreGive(i2c_mutex);
        return err;
    }

    uint16_t mv = 0;
    err = bq27441_read_voltage_mv(s_fg_dev, &mv);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BQ27441 on I2C 0x%02x (V=%u mV probe)", CFG_BQ27441_I2C_ADDR, (unsigned)mv);
    } else {
        ESP_LOGW(TAG, "BQ27441 added but probe read failed: %s", esp_err_to_name(err));
    }

    xSemaphoreGive(i2c_mutex);
    return ESP_OK;
}

void fuel_gauge_task(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t period = pdMS_TO_TICKS(1000);

    while (1) {
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(period);
            continue;
        }

        uint16_t mv = 0;
        uint16_t soc_x10 = 0;
        int16_t current_ma = 0;
        esp_err_t ev = bq27441_read_voltage_mv(s_fg_dev, &mv);
        esp_err_t es = bq27441_read_soc_raw(s_fg_dev, &soc_x10);
        esp_err_t ec = bq27441_read_avg_current_ma(s_fg_dev, &current_ma);

        xSemaphoreGive(i2c_mutex);

        telemetry_battery_t b = {0};
        if (ev == ESP_OK && es == ESP_OK && ec == ESP_OK && mv >= 2000u && mv <= 5000u && soc_x10 <= 1000u) {
            b.valid = true;
            b.voltage_mv = mv;
            b.soc_percent = (float)soc_x10 / 10.0f;
            b.current_ma = current_ma;
        } else {
            b.valid = false;
        }
        telemetry_set_battery(&b);

        vTaskDelay(period);
    }
}
