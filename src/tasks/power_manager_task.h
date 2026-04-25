#ifndef POWER_MANAGER_TASK_H
#define POWER_MANAGER_TASK_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

/** Task handles supplied by main; power_manager is the only owner of mode transitions. */
typedef struct {
    TaskHandle_t gyro;
    TaskHandle_t fuel;
    TaskHandle_t rtk;
    TaskHandle_t display;
    TaskHandle_t onboarding;
} power_manager_handles_t;

typedef enum {
    POWER_MANAGER_MODE_BOOT_CHECK = 0,
    POWER_MANAGER_MODE_CHARGING,
    POWER_MANAGER_MODE_OPERATION,
    POWER_MANAGER_MODE_SHUTDOWN,
} power_manager_mode_t;

/** Configure button/charger inputs and SYSOFF output; drive SYSOFF low. Call before worker tasks. */
esp_err_t power_manager_gpio_init(void);

void power_manager_task(void *pvParameters);

/** Display refresh interval: shorter in operation, longer in charging (low power). */
uint32_t power_manager_display_poll_ms(void);

/** Current power manager mode for UI/state consumers. */
power_manager_mode_t power_manager_current_mode(void);

#endif
