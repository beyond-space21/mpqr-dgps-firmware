#ifndef OPERATION_CONNECTIVITY_H
#define OPERATION_CONNECTIVITY_H

#include "esp_err.h"

/**
 * When the device is provisioned and enters field operation mode, start the
 * temporary SoftAP (if not already up) and the WebSocket server on port 80 (/ws).
 * Idempotent for the current power session.
 */
esp_err_t operation_connectivity_on_operation_enter(void);

#endif
