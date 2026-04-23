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

/** TP_RST: low 20 ms, high 50 ms (releases touch from reset). No-op if CFG_TOUCH_RESET_GPIO < 0. */
void i2c_bus_tp_rst_pulse(void);

/** Log ACKs for 7-bit addresses 0x08-0x77 on the shared bus. */
void i2c_bus_scan(void);

#endif
