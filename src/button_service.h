#ifndef BUTTON_SERVICE_H
#define BUTTON_SERVICE_H

#include "esp_err.h"
#include "freertos/queue.h"

/**
 * GPIO ISR → internal queue → debounce (esp_timer, 50 ms) → long-press timer (2 s).
 * On long-press expiry, posts `SM_EVT_LONG_PRESS` to the state machine queue.
 *
 * Must be initialized after `gpio_install_isr_service` prerequisites: call
 * `power_gpio_init()` first, then `system_fsm_preinit()` so the SM queue exists.
 */
esp_err_t button_service_init(QueueHandle_t sm_event_queue);

/** Creates the worker task that services ISR traffic and timers. */
esp_err_t button_service_start_task(UBaseType_t priority, uint32_t stack_words);

#endif
