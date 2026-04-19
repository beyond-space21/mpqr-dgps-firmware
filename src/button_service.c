#include "button_service.h"
#include "system_fsm.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <stdbool.h>

static const char *TAG = "button_svc";

#define ISR_QUEUE_LEN 16

static QueueHandle_t s_isr_queue;
static QueueHandle_t s_sm_queue;
static esp_timer_handle_t s_debounce_timer;
static esp_timer_handle_t s_longpress_timer;
static TaskHandle_t s_btn_task;

/** Last debounced logical level: 0 = pressed, 1 = released. */
static int s_stable_level = 1;

static void debounce_timer_cb(void *arg);
static void longpress_timer_cb(void *arg);
static void button_task(void *arg);

static void IRAM_ATTR gpio_isr_button(void *arg)
{
    (void)arg;
    BaseType_t hpw = pdFALSE;
    uint32_t stub = 0;
    if (s_isr_queue) {
        (void)xQueueSendFromISR(s_isr_queue, &stub, &hpw);
    }
    if (hpw) {
        portYIELD_FROM_ISR();
    }
}

static void debounce_timer_cb(void *arg)
{
    (void)arg;
    int lvl = gpio_get_level(CFG_BUTTON_GPIO);
    if (lvl == s_stable_level) {
        return;
    }
    s_stable_level = lvl;

    if (lvl == 0) {
        /* Stable press: arm exactly one long-press window. */
        ESP_LOGD(TAG, "debounced PRESS");
        (void)esp_timer_stop(s_longpress_timer);
        esp_err_t err =
            esp_timer_start_once(s_longpress_timer, (uint64_t)CFG_BUTTON_LONG_PRESS_MS * 1000ULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "long-press timer start failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGD(TAG, "debounced RELEASE (cancel long press)");
        (void)esp_timer_stop(s_longpress_timer);
    }
}

static void longpress_timer_cb(void *arg)
{
    (void)arg;
    if (!s_sm_queue) {
        return;
    }
    sm_event_t ev = {
        .id = SM_EVT_LONG_PRESS,
    };
    if (xQueueSend(s_sm_queue, &ev, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "SM queue full; long press dropped");
    } else {
        ESP_LOGI(TAG, "Long press confirmed → SM");
    }
}

static void button_task(void *arg)
{
    (void)arg;
    uint32_t dummy;

    /* Snap initial line level through the same debounce path. */
    s_stable_level = gpio_get_level(CFG_BUTTON_GPIO);
    (void)esp_timer_stop(s_debounce_timer);
    (void)esp_timer_start_once(s_debounce_timer, 1000); /* 1 ms: coalesce any chatter */

    for (;;) {
        if (xQueueReceive(s_isr_queue, &dummy, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        /* Retrigger debounce on every edge; last expiry wins (classic integrator). */
        (void)esp_timer_stop(s_debounce_timer);
        esp_err_t err =
            esp_timer_start_once(s_debounce_timer, (uint64_t)CFG_BUTTON_DEBOUNCE_MS * 1000ULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "debounce timer restart failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t button_service_init(QueueHandle_t sm_event_queue)
{
    if (!sm_event_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    s_sm_queue = sm_event_queue;

    s_isr_queue = xQueueCreate(ISR_QUEUE_LEN, sizeof(uint32_t));
    if (!s_isr_queue) {
        ESP_LOGE(TAG, "ISR queue alloc failed");
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t debounce_args = {
        .callback = debounce_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "btn_debounce",
        .skip_unhandled_events = true,
    };
    esp_err_t err = esp_timer_create(&debounce_args, &s_debounce_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "debounce esp_timer_create failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_timer_create_args_t long_args = {
        .callback = longpress_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "btn_long",
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&long_args, &s_longpress_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "long press esp_timer_create failed: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add(CFG_BUTTON_GPIO, gpio_isr_button, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Button service ready (debounce=%d ms, long=%d ms)", CFG_BUTTON_DEBOUNCE_MS,
             CFG_BUTTON_LONG_PRESS_MS);
    return ESP_OK;
}

esp_err_t button_service_start_task(UBaseType_t priority, uint32_t stack_words)
{
    if (xTaskCreate(button_task, "button", stack_words, NULL, priority, &s_btn_task) != pdPASS) {
        ESP_LOGE(TAG, "button task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
