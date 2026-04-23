#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    WIFI_MANAGER_STATE_OFF = 0,
    WIFI_MANAGER_STATE_SOFTAP_STARTING,
    WIFI_MANAGER_STATE_SOFTAP_READY,
    WIFI_MANAGER_STATE_FAILED,
} wifi_manager_state_t;

typedef struct {
    char ssid[33];
    char password[65];
} wifi_softap_credentials_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start_softap(wifi_softap_credentials_t *out_creds);
esp_err_t wifi_manager_stop_softap(void);
wifi_manager_state_t wifi_manager_get_state(void);
bool wifi_manager_is_ready(void);
wifi_softap_credentials_t wifi_manager_last_credentials(void);

#endif
