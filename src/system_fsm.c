#include "system_fsm.h"
#include "config.h"
#include "power_gpio.h"
#include "i2c_bus.h"
#include "tasks/gyro_task.h"
#include "tasks/fuel_gauge_task.h"
#include "tasks/rtk_task.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include <stdatomic.h>

static const char *TAG = "system_fsm";

#define SM_QUEUE_LEN 8

static QueueHandle_t s_sm_q;
static atomic_int s_run_mode = ATOMIC_VAR_INIT(APP_RUN_MODE_OPERATION);
static app_boot_path_t s_boot_path = BOOT_PATH_OPERATION_IMMEDIATE;

static TaskHandle_t s_th_gyro;
static TaskHandle_t s_th_fuel;
static TaskHandle_t s_th_rtk;

static bool s_workers_created;

static esp_err_t start_sensor_workers(void)
{
    if (s_workers_created) {
        if (s_th_gyro) {
            vTaskResume(s_th_gyro);
        }
        if (s_th_fuel) {
            vTaskResume(s_th_fuel);
        }
        if (s_th_rtk) {
            vTaskResume(s_th_rtk);
        }
        ESP_LOGI(TAG, "Sensor workers resumed");
        return ESP_OK;
    }

    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_bus_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = gyro_peripherals_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gyro_peripherals_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = fuel_gauge_peripherals_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fuel_gauge_peripherals_init failed: %s", esp_err_to_name(err));
        return err;
    }

    const uint32_t stack = 4096;
    const UBaseType_t prio_workers = 5;

    if (xTaskCreatePinnedToCore(gyro_task, "gyro", stack, NULL, prio_workers, &s_th_gyro, 0) != pdPASS) {
        ESP_LOGE(TAG, "gyro task create failed");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(fuel_gauge_task, "fuel", stack, NULL, prio_workers, &s_th_fuel, 0) !=
        pdPASS) {
        ESP_LOGE(TAG, "fuel task create failed");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(rtk_task, "rtk", stack, NULL, prio_workers, &s_th_rtk, 0) != pdPASS) {
        ESP_LOGE(TAG, "rtk task create failed");
        return ESP_ERR_NO_MEM;
    }

    s_workers_created = true;
    ESP_LOGI(TAG, "Sensor workers created (gyro/fuel/rtk on core 0)");
    return ESP_OK;
}

static void suspend_sensor_workers(void)
{
    /*
     * NOTE: FreeRTOS suspend can freeze tasks mid-transaction (I2C/UART). For production,
     * prefer a dedicated "stand down" handshake so drivers finish cleanly before suspend.
     */
    if (!s_workers_created) {
        return;
    }
    if (s_th_gyro) {
        vTaskSuspend(s_th_gyro);
    }
    if (s_th_fuel) {
        vTaskSuspend(s_th_fuel);
    }
    if (s_th_rtk) {
        vTaskSuspend(s_th_rtk);
    }
    ESP_LOGI(TAG, "Sensor workers suspended");
}

static void apply_boot_path(void)
{
    switch (s_boot_path) {
    case BOOT_PATH_CHARGING_IMMEDIATE:
        atomic_store(&s_run_mode, APP_RUN_MODE_CHARGING);
        ESP_LOGI(TAG, "Boot → CHARGING (charger present, button released)");
        break;
    case BOOT_PATH_BUTTON_AWAIT_LONG_PRESS:
        atomic_store(&s_run_mode, APP_RUN_MODE_BOOT_AWAIT_LONG_PRESS);
        ESP_LOGI(TAG, "Boot → await 2 s button hold for OPERATION");
        break;
    case BOOT_PATH_OPERATION_IMMEDIATE:
    default:
        atomic_store(&s_run_mode, APP_RUN_MODE_OPERATION);
        ESP_LOGI(TAG, "Boot → OPERATION (immediate)");
        if (start_sensor_workers() != ESP_OK) {
            ESP_LOGW(TAG, "Worker start failed; staying in OPERATION without sensors");
        }
        break;
    }
}

static void on_long_press(void)
{
    app_run_mode_t mode = (app_run_mode_t)atomic_load(&s_run_mode);

    switch (mode) {
    case APP_RUN_MODE_BOOT_AWAIT_LONG_PRESS:
        if (start_sensor_workers() == ESP_OK) {
            atomic_store(&s_run_mode, APP_RUN_MODE_OPERATION);
            ESP_LOGI(TAG, "Long press while boot-wait → OPERATION");
        } else {
            ESP_LOGW(TAG, "Worker start failed; remain in boot-wait");
        }
        break;

    case APP_RUN_MODE_CHARGING:
        if (start_sensor_workers() == ESP_OK) {
            atomic_store(&s_run_mode, APP_RUN_MODE_OPERATION);
            ESP_LOGI(TAG, "Long press in CHARGING → OPERATION");
        }
        break;

    case APP_RUN_MODE_OPERATION:
        if (power_gpio_charger_connected()) {
            suspend_sensor_workers();
            atomic_store(&s_run_mode, APP_RUN_MODE_CHARGING);
            ESP_LOGI(TAG, "Long press in OPERATION + charger → CHARGING");
        } else {
            ESP_LOGW(TAG, "Long press in OPERATION + battery → shutdown (SYSOFF HIGH)");
            gpio_set_level(CFG_SYSOFF_GPIO, 1);
            /* Latch released; rail should collapse. Hold here in case supply decays slowly. */
            while (1) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        break;

    default:
        break;
    }
}

void system_fsm_preinit(void)
{
    s_sm_q = xQueueCreate(SM_QUEUE_LEN, sizeof(sm_event_t));
    if (!s_sm_q) {
        ESP_LOGE(TAG, "SM queue create failed");
    }
}

void system_fsm_set_boot_path(app_boot_path_t path)
{
    s_boot_path = path;
}

QueueHandle_t system_fsm_get_event_queue(void)
{
    return s_sm_q;
}

app_run_mode_t system_fsm_get_mode(void)
{
    return (app_run_mode_t)atomic_load(&s_run_mode);
}

bool system_fsm_display_use_low_power_refresh(void)
{
    app_run_mode_t m = system_fsm_get_mode();
    return (m == APP_RUN_MODE_CHARGING);
}

void system_fsm_task(void *pvParameters)
{
    (void)pvParameters;

    if (!s_sm_q) {
        ESP_LOGE(TAG, "SM queue missing");
        vTaskDelete(NULL);
        return;
    }

    apply_boot_path();

    sm_event_t ev;
    for (;;) {
        if (xQueueReceive(s_sm_q, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (ev.id == SM_EVT_LONG_PRESS) {
            on_long_press();
        }
    }
}
