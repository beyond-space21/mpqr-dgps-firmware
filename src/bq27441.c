#include "bq27441.h"

esp_err_t bq27441_read_word(i2c_master_dev_handle_t dev, uint8_t cmd, uint16_t *out)
{
    if (!dev || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t rx[2];
    esp_err_t err = i2c_master_transmit_receive(dev, &cmd, 1, rx, sizeof(rx), 100);
    if (err != ESP_OK) {
        return err;
    }
    *out = (uint16_t)rx[0] | ((uint16_t)rx[1] << 8);
    return ESP_OK;
}

esp_err_t bq27441_read_voltage_mv(i2c_master_dev_handle_t dev, uint16_t *mv)
{
    return bq27441_read_word(dev, 0x04, mv);
}

esp_err_t bq27441_read_soc_raw(i2c_master_dev_handle_t dev, uint16_t *soc_x10)
{
    return bq27441_read_word(dev, 0x1C, soc_x10);
}
