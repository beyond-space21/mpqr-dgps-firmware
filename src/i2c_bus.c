#include "i2c_bus.h"
#include "config.h"
#include "sdkconfig.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";

SemaphoreHandle_t i2c_mutex;

static i2c_master_bus_handle_t s_bus;

esp_err_t i2c_bus_init(void)
{
    i2c_mutex = xSemaphoreCreateMutex();
    if (!i2c_mutex) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
        .glitch_ignore_cnt = 7,
        .i2c_port = CFG_I2C_PORT,
        .intr_priority = 0,
        .scl_io_num = CFG_BNO055_SCL_GPIO,
        .sda_io_num = CFG_BNO055_SDA_GPIO,
        .trans_queue_depth = 0,
    };

    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C master bus ready (SDA=%d SCL=%d)", CFG_BNO055_SDA_GPIO, CFG_BNO055_SCL_GPIO);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus;
}
