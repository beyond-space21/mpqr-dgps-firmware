#ifndef OPERATION_CONNECTIVITY_H
#define OPERATION_CONNECTIVITY_H

#include "esp_err.h"

/** Start operation connectivity (BLE remains active, Wi-Fi STA + NTRIP transport path). */
esp_err_t operation_connectivity_on_operation_enter(void);

#endif
