#include "network/rtcm_forward.h"

#include "config.h"
#include "driver/uart.h"

esp_err_t rtcm_forward_to_rtk_uart(const uint8_t *data, size_t len, size_t *written_out)
{
    if (len == 0u || data == NULL) {
        if (written_out != NULL) {
            *written_out = 0u;
        }
        return ESP_OK;
    }
    if (!uart_is_driver_installed(CFG_RTK_UART_NUM)) {
        if (written_out != NULL) {
            *written_out = 0u;
        }
        return ESP_ERR_INVALID_STATE;
    }

    size_t off = 0;
    while (off < len) {
        int n = uart_write_bytes(CFG_RTK_UART_NUM, (const char *)(data + off), (int)(len - off));
        if (n < 0) {
            if (written_out != NULL) {
                *written_out = off;
            }
            return ESP_FAIL;
        }
        if (n == 0) {
            if (written_out != NULL) {
                *written_out = off;
            }
            return ESP_ERR_TIMEOUT;
        }
        off += (size_t)n;
    }

    if (written_out != NULL) {
        *written_out = off;
    }
    return ESP_OK;
}
