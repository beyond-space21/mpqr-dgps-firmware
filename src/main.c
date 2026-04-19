/**
 * DGPS / RTK handheld — ESP32-S3 (ESP-IDF + FreeRTOS)
 *
 * Power / mode overview:
 * - `power_gpio_init()` asserts SYSOFF LOW to keep the PMIC latched, then configures CHG + BUTTON.
 * - A small state machine (`system_fsm`) chooses CHARGING vs OPERATION vs boot confirmation.
 * - `button_service` implements non-blocking debounce + 2 s long press using GPIO ISR + esp_timer.
 *
 * Task topology (priorities are documented in the enums below):
 * - Core 1: display (UI), button edge servicing, state machine (lightweight control plane).
 * - Core 0: RTK UART + IMU + fuel gauge when OPERATION mode enables them.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "power_gpio.h"
#include "system_fsm.h"
#include "button_service.h"
#include "telemetry.h"
#include "tasks/display_task.h"

static const char *TAG = "main";

/**
 * Stacks are in **words** (FreeRTOS `xTaskCreate` convention on ESP-IDF).
 * Control-plane tasks stay modest; sensor UART/parser tasks can grow later.
 */
enum {
    TASK_STACK_WORDS_CTL = 4096,
    PRI_BUTTON = 7,   /* User input path — above workers, below driver ISRs logically */
    PRI_FSM = 6,      /* Mode transitions must preempt UI work */
    PRI_DISPLAY = 3,  /* UI / logging cadence */
};

static app_boot_path_t classify_boot_path(void)
{
    const bool charger = power_gpio_charger_connected();
    const bool pressed = power_gpio_button_pressed();

    if (charger && !pressed) {
        /* Charger applied while button is idle → instant charging UI / low power. */
        return BOOT_PATH_CHARGING_IMMEDIATE;
    }
    if (!charger && pressed) {
        /* Typical “power on with thumb on button” path → require deliberate 2 s hold. */
        return BOOT_PATH_BUTTON_AWAIT_LONG_PRESS;
    }
    if (charger && pressed) {
        /* Ambiguous physical overlap — prefer charger-centric safe mode. */
        ESP_LOGW(TAG, "Boot: charger + button both active → CHARGING");
        return BOOT_PATH_CHARGING_IMMEDIATE;
    }
    /* No charger, button released: watchdog / reset / lab bench power. */
    ESP_LOGW(TAG, "Boot: no charger, button released → OPERATION (fast recovery)");
    return BOOT_PATH_OPERATION_IMMEDIATE;
}

void app_main(void)
{
    ESP_LOGI(TAG, "DGPS init (ESP32-S3 power + mode orchestration)");

    telemetry_init();

    /* 1) Hold power latch first — before any heavy consumers start. */
    ESP_ERROR_CHECK(power_gpio_init());

    const app_boot_path_t boot_path = classify_boot_path();
    ESP_LOGI(TAG, "Boot path enum=%d", (int)boot_path);

    /* 2) Control-plane primitives (queue + GPIO ISR timers). */
    system_fsm_preinit();
    system_fsm_set_boot_path(boot_path);
    if (!system_fsm_get_event_queue()) {
        ESP_LOGE(TAG, "State machine queue missing");
        return;
    }
    ESP_ERROR_CHECK(button_service_init(system_fsm_get_event_queue()));

    /* 3) UI always runs; it adapts cadence + sleep policy to `app_run_mode_t`. */
    if (xTaskCreatePinnedToCore(display_task, "display", TASK_STACK_WORDS_CTL, NULL, PRI_DISPLAY, NULL,
                                1) != pdPASS) {
        ESP_LOGE(TAG, "display task create failed");
        return;
    }

    /* 4) Policy / transitions (sensor workers created/resumed inside FSM as needed). */
    if (xTaskCreatePinnedToCore(system_fsm_task, "sys_fsm", TASK_STACK_WORDS_CTL, NULL, PRI_FSM, NULL,
                                1) != pdPASS) {
        ESP_LOGE(TAG, "system_fsm task create failed");
        return;
    }

    /* 5) Button pipeline (ISR → debounce → long-press). */
    ESP_ERROR_CHECK(button_service_start_task(PRI_BUTTON, TASK_STACK_WORDS_CTL));

    ESP_LOGI(TAG, "Control tasks running; deleting app_main task");
    vTaskDelete(NULL);
}
