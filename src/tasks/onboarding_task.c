#include "tasks/onboarding_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app/onboarding_controller.h"
#include "ble/ble_onboarding_gatt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "network/ws_server.h"
#include "network/wifi_manager.h"
#include "storage/device_settings.h"

static const char *TAG = "onboarding_task";

#define ONBOARDING_TICK_MS 200
#define SOFTAP_NOTIFY_RETRY_MS 400

static const char *state_name(onboarding_state_t s)
{
    switch (s) {
        case ONBOARDING_STATE_IDLE:
            return "IDLE";
        case ONBOARDING_STATE_BLE_ADVERTISING:
            return "BLE_ADVERTISING";
        case ONBOARDING_STATE_PAIR_PENDING_CONFIRM:
            return "PAIR_PENDING_CONFIRM";
        case ONBOARDING_STATE_SOFTAP_STARTING:
            return "SOFTAP_STARTING";
        case ONBOARDING_STATE_SOFTAP_READY:
            return "SOFTAP_READY";
        case ONBOARDING_STATE_CONNECTED:
            return "CONNECTED";
        case ONBOARDING_STATE_FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

void onboarding_task(void *pvParameters)
{
    (void)pvParameters;

    onboarding_state_t last = ONBOARDING_STATE_IDLE;
    bool softap_started = false;
    bool softap_notify_pending = false;
    TickType_t next_softap_notify_retry = 0;
    wifi_softap_credentials_t softap_creds = {0};
    for (;;) {
        const onboarding_status_t st = onboarding_controller_get_status();
        if (st.state != last) {
            ESP_LOGI(TAG, "State transition: %s -> %s", state_name(last), state_name(st.state));
            last = st.state;
            if (st.state == ONBOARDING_STATE_FAILED || st.state == ONBOARDING_STATE_BLE_ADVERTISING ||
                st.state == ONBOARDING_STATE_IDLE) {
                softap_started = false;
                softap_notify_pending = false;
                next_softap_notify_retry = 0;
            }
        }

        if (st.state == ONBOARDING_STATE_PAIR_PENDING_CONFIRM) {
            onboarding_confirm_deadline_t dl = onboarding_controller_get_confirm_deadline();
            const TickType_t now = xTaskGetTickCount();
            if (dl.active && (now - dl.started_at) >= dl.timeout_ticks) {
                ESP_LOGW(TAG, "pair confirmation timeout");
                onboarding_controller_mark_failed();
                (void)ble_onboarding_gatt_notify_error("pair_timeout");
            }
        }

        if (st.state == ONBOARDING_STATE_SOFTAP_STARTING) {
            if (!softap_started) {
                ESP_LOGI(TAG, "Starting SoftAP handoff after user confirmation");
                const esp_err_t err = wifi_manager_start_softap(&softap_creds);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "softap start failed: %s", esp_err_to_name(err));
                    onboarding_controller_mark_failed();
                    (void)ble_onboarding_gatt_notify_error("softap_failed");
                } else {
                    softap_started = true;
                    softap_notify_pending = true;
                    next_softap_notify_retry = xTaskGetTickCount();
                    ESP_LOGI(TAG, "SoftAP ready; scheduling reliable BLE softap_ready notify");
                    onboarding_controller_mark_softap_ready();
                    const esp_err_t ws_err = ws_server_start();
                    if (ws_err == ESP_OK) {
                        ESP_LOGI(TAG, "WebSocket server started at /ws");
                    } else {
                        ESP_LOGW(TAG, "WebSocket server start returned: %s", esp_err_to_name(ws_err));
                    }
                }
            }
        }

        if (st.state == ONBOARDING_STATE_SOFTAP_READY && softap_notify_pending) {
            const TickType_t now = xTaskGetTickCount();
            if (now >= next_softap_notify_retry) {
                const esp_err_t notify_err = ble_onboarding_gatt_notify_softap(&softap_creds);
                if (notify_err == ESP_OK) {
                    ESP_LOGI(TAG, "softap_ready notify delivered to app");
                    softap_notify_pending = false;
                } else {
                    ESP_LOGW(TAG, "softap_ready notify not delivered yet (connected=%d subscribed=%d err=%s); retrying",
                             ble_onboarding_gatt_is_connected() ? 1 : 0,
                             ble_onboarding_gatt_notify_enabled() ? 1 : 0,
                             esp_err_to_name(notify_err));
                    next_softap_notify_retry = now + pdMS_TO_TICKS(SOFTAP_NOTIFY_RETRY_MS);
                }
            }
        } else if (st.state == ONBOARDING_STATE_CONNECTED) {
            ESP_LOGI(TAG, "Persisting provisioned flag in NVS");
            (void)device_settings_set_provisioned(true);
            softap_notify_pending = false;
        }

        vTaskDelay(pdMS_TO_TICKS(ONBOARDING_TICK_MS));
    }
}
