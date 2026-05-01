/**
 * Power manager: BOOT_CHECK → CHARGING_MODE ↔ OPERATION_MODE.
 * Long-press (2 s, debounced 50 ms) via GPIO ISR + FreeRTOS software timers only (no busy waits).
 * In CHARGING_MODE, CFG_CHG_GPIO is polled; unplug (HIGH) for 500 ms after a prior plug → shutdown.
 */
#include "power_manager_task.h"
#include "app/onboarding_controller.h"
#include "ble/ble_onboarding_gatt.h"
#include "network/operation_connectivity.h"
#include "network/wifi_manager.h"
#include "storage/device_settings.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <stdbool.h>
#include <stdint.h>

/** CHG poll interval while in charging mode (no CHG ISR in v1). */
#define POWER_CHG_POLL_MS           50
/** After unplug (CHG HIGH), hold this long before SYSOFF (debounces brief glitches). */
#define POWER_CHG_UNPLUG_SHUTDOWN_MS 500
/** Boot-time PG qualification window to reject brief false-low glitches. */
#define POWER_BOOT_PG_STABLE_MS      120
/** Sampling step used while qualifying PG at boot. */
#define POWER_BOOT_PG_SAMPLE_MS      10

typedef enum {
    EV_LONG_PRESS_CONFIRMED = 0,
    EV_BOOT_BUTTON_RELEASED,
} power_queue_event_t;

typedef enum {
    PWR_STATE_BOOT_CHECK = 0,
    PWR_STATE_CHARGING_MODE,
    PWR_STATE_OPERATION_MODE,
    PWR_STATE_SHUTDOWN,
} power_state_t;

static power_manager_handles_t s_handles;
static QueueHandle_t s_evt_queue;
static TimerHandle_t s_debounce_timer;
static TimerHandle_t s_hold_timer;

static volatile power_state_t s_pwr_state = PWR_STATE_BOOT_CHECK;
static volatile bool s_awaiting_boot_long_press = false;
static volatile uint32_t s_display_poll_ms = 500;

/** In charging: true once CHG was seen LOW (plugged) this session — unplug shutdown only after that. */
static bool s_chg_saw_low_while_charging;
/** In charging: 0 = not timing unplug; else tick when CHG first went HIGH after a prior LOW. */
static TickType_t s_chg_high_since_tick;

static void apply_charging_mode(void);
static void apply_operation_mode(void);
static void start_operation_network_stack(void);
static void enter_shutdown(void);
static void charging_mode_reset_chg_poll(void);
static void poll_chg_while_charging(void);
static bool is_charger_plugged_stable_boot(void);
static void debounce_timer_cb(TimerHandle_t t);
static void hold_timer_cb(TimerHandle_t t);
static void button_gpio_isr(void *arg);

uint32_t power_manager_display_poll_ms(void)
{
    return s_display_poll_ms;
}

power_manager_mode_t power_manager_current_mode(void)
{
    switch (s_pwr_state) {
        case PWR_STATE_CHARGING_MODE:
            return POWER_MANAGER_MODE_CHARGING;
        case PWR_STATE_OPERATION_MODE:
            return POWER_MANAGER_MODE_OPERATION;
        case PWR_STATE_SHUTDOWN:
            return POWER_MANAGER_MODE_SHUTDOWN;
        case PWR_STATE_BOOT_CHECK:
        default:
            return POWER_MANAGER_MODE_BOOT_CHECK;
    }
}

esp_err_t power_manager_gpio_init(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = (1ULL << CFG_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    gpio_config_t chg = {
        .pin_bit_mask = (1ULL << CFG_CHG_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&chg));

    gpio_config_t sysoff = {
        .pin_bit_mask = (1ULL << CFG_SYSOFF_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&sysoff));
    ESP_ERROR_CHECK(gpio_set_level(CFG_SYSOFF_GPIO, 0));

    return ESP_OK;
}

static void suspend_io_tasks(void)
{
    if (s_handles.gyro != NULL) {
        vTaskSuspend(s_handles.gyro);
    }
    if (s_handles.fuel != NULL) {
        vTaskSuspend(s_handles.fuel);
    }
    if (s_handles.rtk != NULL) {
        vTaskSuspend(s_handles.rtk);
    }
    if (s_handles.ntrip != NULL) {
        vTaskSuspend(s_handles.ntrip);
    }
}

static void resume_io_tasks(void)
{
    if (s_handles.gyro != NULL) {
        vTaskResume(s_handles.gyro);
    }
    if (s_handles.fuel != NULL) {
        vTaskResume(s_handles.fuel);
    }
    if (s_handles.rtk != NULL) {
        vTaskResume(s_handles.rtk);
    }
    if (s_handles.ntrip != NULL) {
        vTaskResume(s_handles.ntrip);
    }
}

static void charging_mode_reset_chg_poll(void)
{
    s_chg_high_since_tick = 0;
    s_chg_saw_low_while_charging = (gpio_get_level(CFG_CHG_GPIO) == 0);
}

static void start_operation_network_stack(void)
{
    (void)ble_onboarding_gatt_start_advertising();
    if (!device_settings_is_provisioned()) {
        onboarding_controller_start();
        ESP_LOGI("POWER", "Onboarding state -> BLE_ADVERTISING (unprovisioned)");
    }
    (void)operation_connectivity_on_operation_enter();
}

static void apply_charging_mode(void)
{
    (void)wifi_manager_stop_sta();
    (void)ble_onboarding_gatt_stop_advertising();
    if (s_handles.onboarding != NULL) {
        vTaskSuspend(s_handles.onboarding);
    }
    suspend_io_tasks();
    s_display_poll_ms = 8000;
    s_pwr_state = PWR_STATE_CHARGING_MODE;
    charging_mode_reset_chg_poll();
    ESP_LOGI("POWER", "mode=CHARGING (suspend: onboarding+I/O; stop BLE adv / Wi-Fi; display slow poll)");
}

static void apply_operation_mode(void)
{
    resume_io_tasks();
    if (s_handles.onboarding != NULL) {
        vTaskResume(s_handles.onboarding);
    }
    s_display_poll_ms = 500;
    s_pwr_state = PWR_STATE_OPERATION_MODE;
    ESP_LOGI("POWER", "mode=OPERATION (resume onboarding + I/O; start BLE / network stack)");
    start_operation_network_stack();
}

static void enter_shutdown(void)
{
    ESP_LOGI("POWER", "shutdown: SYSOFF=1");
    esp_err_t err = gpio_set_level(CFG_SYSOFF_GPIO, 1);
    if (err != ESP_OK) {
        ESP_LOGE("POWER", "SYSOFF set high failed: %s", esp_err_to_name(err));
    }
    s_pwr_state = PWR_STATE_SHUTDOWN;
    /* Avoid further GPIO ISR noise after power collapses. */
    gpio_isr_handler_remove(CFG_BUTTON_GPIO);
    gpio_set_intr_type(CFG_BUTTON_GPIO, GPIO_INTR_DISABLE);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void debounce_timer_cb(TimerHandle_t t)
{
    (void)t;
    const int level = gpio_get_level(CFG_BUTTON_GPIO);
    const bool pressed = (level == 0);

    if (pressed) {
        (void)xTimerStop(s_hold_timer, 0);
        (void)xTimerStart(s_hold_timer, 0);
    } else {
        (void)xTimerStop(s_hold_timer, 0);
        if (s_awaiting_boot_long_press) {
            const uint8_t ev = EV_BOOT_BUTTON_RELEASED;
            (void)xQueueSend(s_evt_queue, &ev, 0);
        }
    }
}

static void hold_timer_cb(TimerHandle_t t)
{
    (void)t;
    if (gpio_get_level(CFG_BUTTON_GPIO) != 0) {
        return;
    }
    const uint8_t ev = EV_LONG_PRESS_CONFIRMED;
    (void)xQueueSend(s_evt_queue, &ev, 0);
}

static void poll_chg_while_charging(void)
{
    if (s_pwr_state != PWR_STATE_CHARGING_MODE) {
        return;
    }

    const bool plugged = (gpio_get_level(CFG_CHG_GPIO) == 0);

    if (plugged) {
        s_chg_saw_low_while_charging = true;
        s_chg_high_since_tick = 0;
        return;
    }

    if (!s_chg_saw_low_while_charging) {
        /* e.g. safe charging boot with no cable — do not treat steady HIGH as unplug. */
        return;
    }

    const TickType_t now = xTaskGetTickCount();

    if (s_chg_high_since_tick == 0) {
        s_chg_high_since_tick = now;
        ESP_LOGI("POWER", "CHG unplugged while charging; SYSOFF in %d ms if still unplugged",
                 POWER_CHG_UNPLUG_SHUTDOWN_MS);
        return;
    }

    if ((now - s_chg_high_since_tick) >= pdMS_TO_TICKS(POWER_CHG_UNPLUG_SHUTDOWN_MS)) {
        ESP_LOGI("POWER", "CHG unplugged %d ms → shutdown", POWER_CHG_UNPLUG_SHUTDOWN_MS);
        enter_shutdown();
    }
}

static void IRAM_ATTR button_gpio_isr(void *arg)
{
    (void)arg;
    BaseType_t hpw = pdFALSE;
    if (s_debounce_timer != NULL) {
        (void)xTimerResetFromISR(s_debounce_timer, &hpw);
    }
    if (hpw == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static bool is_charger_plugged_stable_boot(void)
{
    const int samples = (POWER_BOOT_PG_STABLE_MS / POWER_BOOT_PG_SAMPLE_MS);
    for (int i = 0; i < samples; i++) {
        if (gpio_get_level(CFG_CHG_GPIO) != 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(POWER_BOOT_PG_SAMPLE_MS));
    }
    return true;
}

static void run_boot_check(void)
{
    const int chg_level = gpio_get_level(CFG_CHG_GPIO);
    const int btn_level = gpio_get_level(CFG_BUTTON_GPIO);
    const bool charger_plugged = (chg_level == 0) && is_charger_plugged_stable_boot();
    const bool button_pressed = (btn_level == 0);

    ESP_LOGI("POWER", "BOOT_CHECK: CHG=%d BTN=%d", chg_level, btn_level);

    if (charger_plugged) {
        /* Charger at boot: charging mode immediately (tasks already suspended in main). */
        apply_charging_mode();
        s_awaiting_boot_long_press = false;
        return;
    }

    if (button_pressed) {
        /* No charger but button held: need 2 s hold to enter operation; else charging (safe). */
        s_awaiting_boot_long_press = true;
        s_pwr_state = PWR_STATE_BOOT_CHECK;
        ESP_LOGI("POWER", "BOOT_CHECK: awaiting 2 s button hold for OPERATION");
        (void)xTimerStop(s_hold_timer, 0);
        (void)xTimerStart(s_debounce_timer, 0);
        return;
    }

    /* Normal field boot: full operation. */
    s_awaiting_boot_long_press = false;
    apply_operation_mode();
}

static void on_long_press(void)
{
    if (s_awaiting_boot_long_press) {
        s_awaiting_boot_long_press = false;
        apply_operation_mode();
        return;
    }

    if (s_pwr_state == PWR_STATE_CHARGING_MODE) {
        apply_operation_mode();
        return;
    }

    if (s_pwr_state == PWR_STATE_OPERATION_MODE) {
        const bool charger_plugged = (gpio_get_level(CFG_CHG_GPIO) == 0);
        if (charger_plugged) {
            apply_charging_mode();
        } else {
            enter_shutdown();
        }
        return;
    }
}

void power_manager_task(void *pvParameters)
{
    if (pvParameters == NULL) {
        ESP_LOGE("POWER", "power_manager_task: null pvParameters");
        return;
    }
    s_handles = *(const power_manager_handles_t *)pvParameters;

    s_evt_queue = xQueueCreate(8, sizeof(uint8_t));
    if (s_evt_queue == NULL) {
        ESP_LOGE("POWER", "event queue create failed");
        return;
    }

    s_debounce_timer = xTimerCreate("pm_db", pdMS_TO_TICKS(50), pdFALSE, NULL, debounce_timer_cb);
    s_hold_timer = xTimerCreate("pm_hold", pdMS_TO_TICKS(2000), pdFALSE, NULL, hold_timer_cb);
    if (s_debounce_timer == NULL || s_hold_timer == NULL) {
        ESP_LOGE("POWER", "timer create failed");
        return;
    }

    ESP_ERROR_CHECK(gpio_set_intr_type(CFG_BUTTON_GPIO, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CFG_BUTTON_GPIO, button_gpio_isr, NULL));

    run_boot_check();

    for (;;) {
        const TickType_t wait_ticks =
            (s_pwr_state == PWR_STATE_CHARGING_MODE) ? pdMS_TO_TICKS(POWER_CHG_POLL_MS) : portMAX_DELAY;

        uint8_t ev = 0;
        const BaseType_t got = xQueueReceive(s_evt_queue, &ev, wait_ticks);

        if (s_pwr_state == PWR_STATE_SHUTDOWN) {
            continue;
        }

        if (got == pdTRUE) {
            if (ev == EV_BOOT_BUTTON_RELEASED) {
                if (s_awaiting_boot_long_press) {
                    s_awaiting_boot_long_press = false;
                    const bool charger_plugged = (gpio_get_level(CFG_CHG_GPIO) == 0);
                    if (charger_plugged) {
                        ESP_LOGI("POWER", "BOOT_CHECK: button released before 2 s with CHG present -> CHARGING");
                        apply_charging_mode();
                    } else {
                        ESP_LOGI("POWER", "BOOT_CHECK: button released before 2 s with no CHG -> SHUTDOWN");
                        enter_shutdown();
                    }
                }
            } else if (ev == EV_LONG_PRESS_CONFIRMED) {
                ESP_LOGI("POWER", "long press confirmed");
                on_long_press();
            }
        }

        poll_chg_while_charging();
    }
}
