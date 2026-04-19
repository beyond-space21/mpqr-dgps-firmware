#ifndef BQ27441_H
#define BQ27441_H

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

/**
 * BQ27441-G1A standard command reads (TI SLUUAD6).
 * One-byte command offset, then 16-bit little-endian value.
 */
esp_err_t bq27441_read_word(i2c_master_dev_handle_t dev, uint8_t cmd, uint16_t *out);

esp_err_t bq27441_read_voltage_mv(i2c_master_dev_handle_t dev, uint16_t *mv);

/** State of charge 0.1 % units (0–1000). */
esp_err_t bq27441_read_soc_raw(i2c_master_dev_handle_t dev, uint16_t *soc_x10);

#endif
