#ifndef RTCM_FORWARD_H
#define RTCM_FORWARD_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t rtcm_forward_to_rtk_uart(const uint8_t *data, size_t len, size_t *written_out);

#endif
