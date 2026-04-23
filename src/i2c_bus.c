#include "i2c_bus.h"
#include "config.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

void i2c_bus_tp_rst_pulse(void)
{
#if CFG_TOUCH_RESET_GPIO >= 0
    const int rst = CFG_TOUCH_RESET_GPIO;
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << rst,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(rst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "TP_RST GPIO%d: low 20 ms, high 50 ms", rst);
#else
    ESP_LOGD(TAG, "TP_RST pulse skipped (CFG_TOUCH_RESET_GPIO < 0)");
#endif
}

void i2c_bus_scan(void)
{
    if (!s_bus) {
        ESP_LOGW(TAG, "I2C scan: bus not initialized");
        return;
    }

    ESP_LOGI(TAG, "I2C scan 7-bit 0x08-0x77 (SDA=%d SCL=%d)", CFG_BNO055_SDA_GPIO, CFG_BNO055_SCL_GPIO);

    int found = 0;
    for (unsigned addr = 0x08; addr <= 0x77; addr++) {
        esp_err_t err = i2c_master_probe(s_bus, (uint16_t)addr, 50 /* ms */);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  ACK at 0x%02x", addr);
            found++;
        }
    }

    if (found == 0) {
        ESP_LOGW(TAG, "I2C scan: no devices");
    } else {
        ESP_LOGI(TAG, "I2C scan: %d device(s)", found);
    }
}
