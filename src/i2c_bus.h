#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/** Global bus mutex: take before any I2C transaction, give after. */
extern SemaphoreHandle_t i2c_mutex;

esp_err_t i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus_get_handle(void);

#endif
