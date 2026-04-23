#ifndef WS_SERVER_H
#define WS_SERVER_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t ws_server_start(void);
esp_err_t ws_server_stop(void);
bool ws_server_is_running(void);

#endif
