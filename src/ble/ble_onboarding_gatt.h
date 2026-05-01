#ifndef BLE_ONBOARDING_GATT_H
#define BLE_ONBOARDING_GATT_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t ble_onboarding_gatt_init(void);
esp_err_t ble_onboarding_gatt_start_advertising(void);
esp_err_t ble_onboarding_gatt_stop_advertising(void);
esp_err_t ble_onboarding_gatt_notify_link_ready(void);
esp_err_t ble_onboarding_gatt_notify_telemetry_json(const char *line);
esp_err_t ble_onboarding_gatt_notify_pair_pending(const char *code4);
esp_err_t ble_onboarding_gatt_notify_error(const char *reason);
bool ble_onboarding_gatt_is_connected(void);
bool ble_onboarding_gatt_notify_enabled(void);
bool ble_onboarding_gatt_telemetry_notify_enabled(void);

#endif
