#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "touch_task.h"
#include "config.h"
#include "esp_log.h"

static const char *TAG = "touch_task";

void touch_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Touch I2C placeholder (addr 0x%02x) — wire controller then take i2c_mutex",
             CFG_TOUCH_I2C_ADDR);

    while (1) {
        /* Future: xSemaphoreTake(i2c_mutex); read touch; xSemaphoreGive(i2c_mutex); */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
