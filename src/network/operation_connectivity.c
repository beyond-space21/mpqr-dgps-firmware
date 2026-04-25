#include "network/operation_connectivity.h"

#include "esp_log.h"
#include "network/wifi_manager.h"
#include "network/ws_server.h"
#include "storage/device_settings.h"

static const char *TAG = "op_connect";

esp_err_t operation_connectivity_on_operation_enter(void)
{
    if (!device_settings_is_provisioned()) {
        return ESP_OK;
    }

    wifi_softap_credentials_t creds = {0};
    const esp_err_t werr = wifi_manager_start_softap(&creds);
    if (werr != ESP_OK) {
        ESP_LOGE(TAG, "SoftAP start failed: %s", esp_err_to_name(werr));
        return werr;
    }

    const esp_err_t ws_err = ws_server_start();
    if (ws_err != ESP_OK) {
        ESP_LOGW(TAG, "WebSocket server start: %s", esp_err_to_name(ws_err));
    } else {
        ESP_LOGI(TAG, "WebSocket /ws on 192.168.4.1:80; BLE pair_request -> softap_ready for credentials");
    }

    return ESP_OK;
}
