#include "tasks/onboarding_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app/onboarding_controller.h"
#include "ble/ble_onboarding_gatt.h"
#include "ble/onboarding_protocol.h"
#include "esp_err.h"
#include "esp_log.h"
#include "storage/device_settings.h"
#include "telemetry.h"

static const char *TAG = "onboarding_task";

#define ONBOARDING_TICK_MS 200
#define LINK_NOTIFY_RETRY_MS 400
#define TELEMETRY_NOTIFY_MS 1000

static const char *state_name(onboarding_state_t s)
{
    switch (s) {
        case ONBOARDING_STATE_IDLE:
            return "IDLE";
        case ONBOARDING_STATE_BLE_ADVERTISING:
            return "BLE_ADVERTISING";
        case ONBOARDING_STATE_PAIR_PENDING_CONFIRM:
            return "PAIR_PENDING_CONFIRM";
        case ONBOARDING_STATE_LINK_STARTING:
            return "LINK_STARTING";
        case ONBOARDING_STATE_LINK_READY:
            return "LINK_READY";
        case ONBOARDING_STATE_CONNECTED:
            return "CONNECTED";
        case ONBOARDING_STATE_FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

static const char *wifi_status_str(telemetry_wifi_state_t st)
{
    switch (st) {
        case TELEMETRY_WIFI_CONNECTING:
            return "connecting";
        case TELEMETRY_WIFI_CONNECTED:
            return "connected";
        case TELEMETRY_WIFI_FAILED:
            return "failed";
        case TELEMETRY_WIFI_OFF:
        default:
            return "off";
    }
}

static void telemetry_emit_line_cb(const char *line)
{
    (void)ble_onboarding_gatt_notify_telemetry_json(line);
}

static const char *ntrip_status_str(telemetry_ntrip_state_t st)
{
    switch (st) {
        case TELEMETRY_NTRIP_CONNECTING:
            return "connecting";
        case TELEMETRY_NTRIP_STREAMING:
            return "streaming";
        case TELEMETRY_NTRIP_AUTH_FAILED:
            return "auth_failed";
        case TELEMETRY_NTRIP_ERROR:
            return "error";
        case TELEMETRY_NTRIP_DISCONNECTED:
        default:
            return "disconnected";
    }
}

void onboarding_task(void *pvParameters)
{
    (void)pvParameters;

    onboarding_state_t last = ONBOARDING_STATE_IDLE;
    bool link_ready_notified = false;
    bool link_notify_pending = false;
    TickType_t next_link_notify_retry = 0;
    TickType_t next_telemetry_notify = 0;
    for (;;) {
        const onboarding_status_t st = onboarding_controller_get_status();
        if (st.state != last) {
            ESP_LOGI(TAG, "State transition: %s -> %s", state_name(last), state_name(st.state));
            const onboarding_state_t prev = last;
            last = st.state;

            if (st.state == ONBOARDING_STATE_LINK_STARTING) {
                link_ready_notified = false;
                link_notify_pending = false;
                next_link_notify_retry = 0;
            }
            if (st.state == ONBOARDING_STATE_CONNECTED && prev != ONBOARDING_STATE_CONNECTED) {
                ESP_LOGI(TAG, "Persisting provisioned flag in NVS");
                (void)device_settings_set_provisioned(true);
            }
            if (st.state == ONBOARDING_STATE_FAILED || st.state == ONBOARDING_STATE_BLE_ADVERTISING ||
                st.state == ONBOARDING_STATE_IDLE) {
                link_ready_notified = false;
                link_notify_pending = false;
                next_link_notify_retry = 0;
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

        if (st.state == ONBOARDING_STATE_LINK_STARTING) {
            link_notify_pending = true;
            next_link_notify_retry = xTaskGetTickCount();
            onboarding_controller_mark_link_ready();
        }

        if (st.state == ONBOARDING_STATE_LINK_READY && link_notify_pending) {
            const TickType_t now = xTaskGetTickCount();
            if (now >= next_link_notify_retry) {
                const esp_err_t notify_err = ble_onboarding_gatt_notify_link_ready();
                if (notify_err == ESP_OK) {
                    ESP_LOGI(TAG, "ble_link_ready notify delivered to app");
                    link_notify_pending = false;
                    link_ready_notified = true;
                    onboarding_controller_mark_connected();
                } else {
                    ESP_LOGW(TAG, "ble_link_ready notify not delivered yet (connected=%d subscribed=%d err=%s); retrying",
                             ble_onboarding_gatt_is_connected() ? 1 : 0,
                             ble_onboarding_gatt_notify_enabled() ? 1 : 0,
                             esp_err_to_name(notify_err));
                    next_link_notify_retry = now + pdMS_TO_TICKS(LINK_NOTIFY_RETRY_MS);
                }
            }
        } else if (st.state == ONBOARDING_STATE_CONNECTED) {
            link_notify_pending = false;
            if (!link_ready_notified) {
                link_ready_notified = true;
            }
        }

        if ((st.state == ONBOARDING_STATE_CONNECTED || device_settings_is_provisioned()) &&
            ble_onboarding_gatt_is_connected() &&
            ble_onboarding_gatt_telemetry_notify_enabled()) {
            const TickType_t now = xTaskGetTickCount();
            if (now >= next_telemetry_notify) {
                telemetry_t t;
                telemetry_get_copy(&t);

                onboarding_protocol_dispatch_telemetry(
                    telemetry_emit_line_cb, telemetry_rtk_quality_str(&t.rtk), t.rtk.h_acc_m, t.rtk.v_acc_m,
                    t.rtk.lat_deg, t.rtk.lon_deg, t.rtk.valid ? t.rtk.h_msl_m : 0.f,
                    (unsigned)(t.rtk.num_sv_visible ? t.rtk.num_sv_visible : t.rtk.num_sv),
                    wifi_status_str(t.wifi.state), t.wifi.ssid, t.wifi.ip,
                    ntrip_status_str(t.ntrip.state), t.ntrip.last_error);
                next_telemetry_notify = now + pdMS_TO_TICKS(TELEMETRY_NOTIFY_MS);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(ONBOARDING_TICK_MS));
    }
}
