#include "app/app_bootstrap.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "telemetry.h"
#include "storage/device_settings.h"
#include "app/onboarding_controller.h"
#include "ble/ble_onboarding_gatt.h"
#include "network/wifi_manager.h"
#include "tasks/display_task.h"
#include "tasks/fuel_gauge_task.h"
#include "tasks/gyro_task.h"
#include "tasks/onboarding_task.h"
#include "tasks/power_manager_task.h"
#include "tasks/rtk_task.h"

static const char *TAG = "bootstrap";

enum {
    TASK_STACK_DEFAULT = 4096,
    PRI_POWER = 6,
    PRI_GYRO = 5,
    PRI_FUEL = 5,
    PRI_RTK = 5,
    PRI_ONBOARDING = 4,
    PRI_DISPLAY = 3,
};

void app_bootstrap_start(void)
{
    /* One install for all GPIO ISRs (power button + optional touch). */
    {
        const esp_err_t isr = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(isr));
            return;
        }
    }

    ESP_ERROR_CHECK(power_manager_gpio_init());

    telemetry_init();
    ESP_ERROR_CHECK(device_settings_init());
    onboarding_controller_init();
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(ble_onboarding_gatt_init());

    if (i2c_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return;
    }

    i2c_bus_tp_rst_pulse();
    i2c_bus_scan();

    if (gyro_peripherals_init() != ESP_OK) {
        ESP_LOGE(TAG, "Gyro peripherals init failed");
        return;
    }

    if (fuel_gauge_peripherals_init() != ESP_OK) {
        ESP_LOGE(TAG, "Fuel gauge peripherals init failed");
        return;
    }

    static power_manager_handles_t s_pm_handles;

    /* Ensure IO tasks start suspended before mode selection. */
    const UBaseType_t saved_prio = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, (UBaseType_t)(configMAX_PRIORITIES - 1));
    xTaskCreatePinnedToCore(gyro_task, "gyro", TASK_STACK_DEFAULT, NULL, PRI_GYRO, &s_pm_handles.gyro, 1);
    vTaskSuspend(s_pm_handles.gyro);
    xTaskCreatePinnedToCore(fuel_gauge_task, "fuel", TASK_STACK_DEFAULT, NULL, PRI_FUEL, &s_pm_handles.fuel, 1);
    vTaskSuspend(s_pm_handles.fuel);
    xTaskCreatePinnedToCore(rtk_task, "rtk", TASK_STACK_DEFAULT, NULL, PRI_RTK, &s_pm_handles.rtk, 1);
    vTaskSuspend(s_pm_handles.rtk);
    vTaskPrioritySet(NULL, saved_prio);

    xTaskCreatePinnedToCore(display_task, "display", TASK_STACK_DEFAULT, NULL, PRI_DISPLAY, &s_pm_handles.display, 1);
    xTaskCreatePinnedToCore(power_manager_task, "power", TASK_STACK_DEFAULT, &s_pm_handles, PRI_POWER, NULL, 1);
    xTaskCreatePinnedToCore(onboarding_task, "onboarding", TASK_STACK_DEFAULT, NULL, PRI_ONBOARDING, NULL, 1);

    const bool provisioned = device_settings_is_provisioned();
    (void)ble_onboarding_gatt_start_advertising();
    if (!provisioned) {
        onboarding_controller_start();
        ESP_LOGI(TAG, "Onboarding flow started (device unprovisioned)");
    } else {
        ESP_LOGI(TAG, "Provisioned device: BLE advertising enabled for fast reconnect");
    }
}
