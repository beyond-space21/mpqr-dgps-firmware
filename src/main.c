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
#include "tasks/power_manager_task.h"

static const char *TAG = "main";

enum {
    TASK_STACK_DEFAULT = 4096,
    TASK_STACK_NET = 8192,
    PRI_POWER = 6,
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

    ESP_ERROR_CHECK(power_manager_gpio_init());

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

    static power_manager_handles_t s_pm_handles;

    /* Raise main priority so gyro/fuel/rtk cannot run until suspended (they outrank app_main by default). */
    const UBaseType_t saved_prio = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, (UBaseType_t)(configMAX_PRIORITIES - 1));

    /* Core 1: gyro (IMU) + fuel gauge (shared I2C) + RTK UART — start suspended until power_manager decides mode */
    xTaskCreatePinnedToCore(gyro_task, "gyro", TASK_STACK_DEFAULT, NULL, PRI_GYRO, &s_pm_handles.gyro, 1);
    vTaskSuspend(s_pm_handles.gyro);
    xTaskCreatePinnedToCore(fuel_gauge_task, "fuel", TASK_STACK_DEFAULT, NULL, PRI_FUEL, &s_pm_handles.fuel, 1);
    vTaskSuspend(s_pm_handles.fuel);
    xTaskCreatePinnedToCore(rtk_task, "rtk", TASK_STACK_DEFAULT, NULL, PRI_RTK, &s_pm_handles.rtk, 1);
    vTaskSuspend(s_pm_handles.rtk);

    vTaskPrioritySet(NULL, saved_prio);

    /* Core 1: network + UI */
    // xTaskCreatePinnedToCore(ntrip_task, "ntrip", TASK_STACK_NET, NULL, PRI_NTRIP, NULL, 1);
    xTaskCreatePinnedToCore(display_task, "display", TASK_STACK_DEFAULT, NULL, PRI_DISPLAY, &s_pm_handles.display, 1);
    // xTaskCreatePinnedToCore(touch_task, "touch", TASK_STACK_DEFAULT, NULL, PRI_TOUCH, NULL, 1);

    xTaskCreatePinnedToCore(power_manager_task, "power", TASK_STACK_DEFAULT, &s_pm_handles, PRI_POWER, NULL, 1);

    ESP_LOGI(TAG, "All tasks started");
    vTaskDelete(NULL);
}
