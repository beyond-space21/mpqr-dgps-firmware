#ifndef DEVICE_SETTINGS_H
#define DEVICE_SETTINGS_H

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t device_settings_init(void);
esp_err_t device_settings_set_provisioned(bool provisioned);
bool device_settings_is_provisioned(void);
esp_err_t device_settings_get_device_id(char *out, size_t out_len);

#endif
