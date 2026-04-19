/**
 * Power manager: BOOT_CHECK → CHARGING_MODE ↔ OPERATION_MODE.
 * Long-press (2 s, debounced 50 ms) via GPIO ISR + FreeRTOS software timers only (no busy waits).
 * In CHARGING_MODE, CFG_CHG_GPIO is polled; unplug (HIGH) for 500 ms after a prior plug → shutdown.
 */
#include "power_manager_task.h"
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
static void enter_shutdown(void);
static void charging_mode_reset_chg_poll(void);
static void poll_chg_while_charging(void);
static void debounce_timer_cb(TimerHandle_t t);
static void hold_timer_cb(TimerHandle_t t);
static void button_gpio_isr(void *arg);

uint32_t power_manager_display_poll_ms(void)
{
    return s_display_poll_ms;
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
}

static void charging_mode_reset_chg_poll(void)
{
    s_chg_high_since_tick = 0;
    s_chg_saw_low_while_charging = (gpio_get_level(CFG_CHG_GPIO) == 0);
}

static void apply_charging_mode(void)
{
    suspend_io_tasks();
    s_display_poll_ms = 8000;
    s_pwr_state = PWR_STATE_CHARGING_MODE;
    charging_mode_reset_chg_poll();
    ESP_LOGI("POWER", "mode=CHARGING (gyro/fuel/rtk suspended, display slow poll)");
}

static void apply_operation_mode(void)
{
    resume_io_tasks();
    s_display_poll_ms = 500;
    s_pwr_state = PWR_STATE_OPERATION_MODE;
    ESP_LOGI("POWER", "mode=OPERATION (all I/O tasks running)");
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

static void run_boot_check(void)
{
    const int chg_level = gpio_get_level(CFG_CHG_GPIO);
    const int btn_level = gpio_get_level(CFG_BUTTON_GPIO);
    const bool charger_plugged = (chg_level == 0);
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

    esp_err_t isre = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (isre != ESP_OK && isre != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("POWER", "gpio_install_isr_service: %s", esp_err_to_name(isre));
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
                    ESP_LOGI("POWER", "BOOT_CHECK: button released before 2 s → CHARGING (safe)");
                    apply_charging_mode();
                }
            } else if (ev == EV_LONG_PRESS_CONFIRMED) {
                ESP_LOGI("POWER", "long press confirmed");
                on_long_press();
            }
        }

        poll_chg_while_charging();
    }
}
