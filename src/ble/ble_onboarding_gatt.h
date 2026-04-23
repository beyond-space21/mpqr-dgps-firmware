#ifndef BLE_ONBOARDING_GATT_H
#define BLE_ONBOARDING_GATT_H

#include <stdbool.h>
#include "esp_err.h"
#include "network/wifi_manager.h"

esp_err_t ble_onboarding_gatt_init(void);
esp_err_t ble_onboarding_gatt_start_advertising(void);
esp_err_t ble_onboarding_gatt_stop_advertising(void);
esp_err_t ble_onboarding_gatt_notify_softap(const wifi_softap_credentials_t *creds);
esp_err_t ble_onboarding_gatt_notify_pair_pending(const char *code4);
esp_err_t ble_onboarding_gatt_notify_error(const char *reason);
bool ble_onboarding_gatt_is_connected(void);
bool ble_onboarding_gatt_notify_enabled(void);

#endif
