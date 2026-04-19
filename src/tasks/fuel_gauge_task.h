#ifndef FUEL_GAUGE_TASK_H
#define FUEL_GAUGE_TASK_H

#include "esp_err.h"

/** Add BQ27441 on the shared I2C bus (after i2c_bus_init; before tasks). */
esp_err_t fuel_gauge_peripherals_init(void);

void fuel_gauge_task(void *pvParameters);

#endif
