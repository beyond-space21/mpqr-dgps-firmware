#ifndef SYSTEM_FSM_H
#define SYSTEM_FSM_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>

/**
 * High-level run mode exposed to UI / sensors.
 * - BOOT_AWAIT_LONG_PRESS: power-on via button; require 2 s hold before OPERATION.
 * - CHARGING: display only; heavy peripherals idle or absent.
 * - OPERATION: display + RTK + gyro + fuel gauge.
 */
typedef enum {
    APP_RUN_MODE_BOOT_AWAIT_LONG_PRESS = 0,
    APP_RUN_MODE_CHARGING,
    APP_RUN_MODE_OPERATION,
} app_run_mode_t;

typedef enum {
    SM_EVT_LONG_PRESS = 0,
} sm_event_id_t;

typedef struct {
    sm_event_id_t id;
} sm_event_t;

/** How app_main classified reset / power-on pins. */
typedef enum {
    BOOT_PATH_CHARGING_IMMEDIATE = 0,
    BOOT_PATH_BUTTON_AWAIT_LONG_PRESS,
    BOOT_PATH_OPERATION_IMMEDIATE,
} app_boot_path_t;

/** Create SM queue and internal state; does not start the task. */
void system_fsm_preinit(void);

/** Stash boot path (call before starting `system_fsm_task`). */
void system_fsm_set_boot_path(app_boot_path_t path);

/** Queue consumed by the state machine task (ISR/timer paths must not block it). */
QueueHandle_t system_fsm_get_event_queue(void);

/** Latest mode for display and diagnostics (written only by SM task). */
app_run_mode_t system_fsm_get_mode(void);

/** True when display should minimize refresh and may use light sleep between frames. */
bool system_fsm_display_use_low_power_refresh(void);

/**
 * Main state machine: applies boot path, then processes `sm_event_t` (long press).
 * Stack/priority chosen in app_main.
 */
void system_fsm_task(void *pvParameters);

#endif
