#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    WIFI_MANAGER_STATE_OFF = 0,
    WIFI_MANAGER_STATE_CONNECTING,
    WIFI_MANAGER_STATE_CONNECTED,
    WIFI_MANAGER_STATE_FAILED,
} wifi_manager_state_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start_sta(void);
esp_err_t wifi_manager_stop_sta(void);
wifi_manager_state_t wifi_manager_get_state(void);
bool wifi_manager_is_connected(void);
const char *wifi_manager_get_ssid(void);
const char *wifi_manager_get_ip(void);

#endif
