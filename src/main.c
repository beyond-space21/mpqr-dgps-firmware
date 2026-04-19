#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "telemetry.h"
#include "tasks/gyro_task.h"
#include "tasks/fuel_gauge_task.h"
#include "tasks/rtk_task.h"
#include "tasks/ntrip_task.h"
#include "tasks/display_task.h"
#include "tasks/touch_task.h"

static const char *TAG = "main";

enum {
    TASK_STACK_DEFAULT = 4096,
    TASK_STACK_NET = 8192,
    PRI_GYRO = 5,
    PRI_FUEL = 5,
    PRI_RTK = 5,
    PRI_NTRIP = 4,
    PRI_TOUCH = 4,
    PRI_DISPLAY = 3,
};

void app_main(void)
{
    ESP_LOGI(TAG, "DGPS init");

    telemetry_init();

    if (i2c_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return;
    }

    if (gyro_peripherals_init() != ESP_OK) {
        ESP_LOGE(TAG, "Gyro peripherals init failed");
        return;
    }

    if (fuel_gauge_peripherals_init() != ESP_OK) {
        ESP_LOGE(TAG, "Fuel gauge peripherals init failed");
        return;
    }

    /* Core 0: gyro (IMU) + fuel gauge (shared I2C) + RTK UART */
    xTaskCreatePinnedToCore(gyro_task, "gyro", TASK_STACK_DEFAULT, NULL, PRI_GYRO, NULL, 0);
    xTaskCreatePinnedToCore(fuel_gauge_task, "fuel", TASK_STACK_DEFAULT, NULL, PRI_FUEL, NULL, 0);
    xTaskCreatePinnedToCore(rtk_task, "rtk", TASK_STACK_DEFAULT, NULL, PRI_RTK, NULL, 0);

    /* Core 1: network + UI */
    // xTaskCreatePinnedToCore(ntrip_task, "ntrip", TASK_STACK_NET, NULL, PRI_NTRIP, NULL, 1);
    xTaskCreatePinnedToCore(display_task, "display", TASK_STACK_DEFAULT, NULL, PRI_DISPLAY, NULL, 1);
    // xTaskCreatePinnedToCore(touch_task, "touch", TASK_STACK_DEFAULT, NULL, PRI_TOUCH, NULL, 1);

    ESP_LOGI(TAG, "All tasks started");
    vTaskDelete(NULL);
}
