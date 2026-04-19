#ifndef GYRO_TASK_H
#define GYRO_TASK_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/** Register BNO055 on the shared bus and initialize (call after i2c_bus_init, before tasks). */
esp_err_t gyro_peripherals_init(void);

void gyro_task(void *pvParameters);

#endif
