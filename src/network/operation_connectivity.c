#include "network/operation_connectivity.h"

#include "esp_log.h"
#include "network/wifi_manager.h"
static const char *TAG = "op_connect";

esp_err_t operation_connectivity_on_operation_enter(void)
{
    const esp_err_t werr = wifi_manager_start_sta();
    if (werr != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi STA start failed: %s", esp_err_to_name(werr));
        return werr;
    }
    ESP_LOGI(TAG, "Operation connectivity active: BLE link + Wi-Fi STA for NTRIP");

    return ESP_OK;
}
