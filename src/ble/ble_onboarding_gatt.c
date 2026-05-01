#include "ble/ble_onboarding_gatt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app/onboarding_controller.h"
#include "ble/onboarding_protocol.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "storage/device_settings.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif

static bool s_connected;
static const char *TAG = "ble_onboard";
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
#else
static uint16_t s_conn_handle = 0xFFFF;
#endif
static uint16_t s_notify_handle;
static uint16_t s_telemetry_notify_handle;
static char s_rx_line_buf[512];
static size_t s_rx_line_len;
static bool s_notify_enabled;
static bool s_telemetry_notify_enabled;

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
static const ble_uuid128_t UUID_SVC = BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                                       0x00, 0x10, 0x00, 0x00, 0xF0, 0xFF, 0x00, 0x00);
static const ble_uuid128_t UUID_INFO = BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                                        0x00, 0x10, 0x00, 0x00, 0xF1, 0xFF, 0x00, 0x00);
static const ble_uuid128_t UUID_CTRL = BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                                        0x00, 0x10, 0x00, 0x00, 0xF3, 0xFF, 0x00, 0x00);
static const ble_uuid128_t UUID_NOTIFY = BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                                          0x00, 0x10, 0x00, 0x00, 0xF4, 0xFF, 0x00, 0x00);
static const ble_uuid128_t UUID_TELEMETRY = BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                                             0x00, 0x10, 0x00, 0x00, 0xF5, 0xFF, 0x00, 0x00);
#endif

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
#define INVALID_CONN_HANDLE BLE_HS_CONN_HANDLE_NONE
#else
#define INVALID_CONN_HANDLE 0xFFFF
#endif

static esp_err_t notify_json_line_with_handle(const char *line, uint16_t handle, bool enabled)
{
    if (line == NULL || s_conn_handle == INVALID_CONN_HANDLE || handle == 0 || !enabled) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
    struct os_mbuf *om = ble_hs_mbuf_from_flat(line, strlen(line));
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }
    int rc = ble_gattc_notify_custom(s_conn_handle, handle, om);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
#else
    ESP_LOGI(TAG, "notify(stub): %s", line);
    return ESP_OK;
#endif
}

static esp_err_t notify_json_line(const char *line)
{
    return notify_json_line_with_handle(line, s_notify_handle, s_notify_enabled);
}

static void handle_control_line(const char *line)
{
    if (line == NULL) {
        return;
    }
    ESP_LOGI(TAG, "Control line received: %s", line);

    onboarding_control_cmd_t cmd = {0};
    if (!onboarding_protocol_parse_control_line(line, &cmd)) {
        ESP_LOGW(TAG, "Invalid onboarding control request");
        (void)ble_onboarding_gatt_notify_error("invalid_request");
        return;
    }

    if (cmd.type == ONBOARDING_CMD_PAIR_REQUEST) {
        ESP_LOGI(TAG, "pair_request received (code=%s)", cmd.code);
        onboarding_status_t st = onboarding_controller_get_status();
        if (device_settings_is_provisioned() && st.state != ONBOARDING_STATE_PAIR_PENDING_CONFIRM) {
            ESP_LOGI(TAG, "Fast reconnect: pair_pending_confirm then auto-accept");
            (void)ble_onboarding_gatt_notify_pair_pending(cmd.code);
            onboarding_controller_confirm_pairing(true);
            return;
        }
        onboarding_controller_set_pair_code(cmd.code);
        (void)ble_onboarding_gatt_notify_pair_pending(cmd.code);
        return;
    }
    (void)ble_onboarding_gatt_notify_error("invalid_request");
}

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            s_connected = (event->connect.status == 0);
            s_conn_handle = s_connected ? event->connect.conn_handle : BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGI(TAG, "NimBLE GAP: CONNECT status=%d conn_handle=%u (peripheral role)",
                     event->connect.status, (unsigned)s_conn_handle);
            if (!s_connected) {
                (void)ble_onboarding_gatt_start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            s_connected = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_rx_line_len = 0;
            s_notify_enabled = false;
            s_telemetry_notify_enabled = false;
            ESP_LOGI(TAG, "NimBLE GAP: DISCONNECT (events+telemetry notify reset); restarting adv");
            (void)ble_onboarding_gatt_start_advertising();
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_notify_handle) {
                s_notify_enabled = event->subscribe.cur_notify != 0;
                ESP_LOGI(TAG,
                         "NimBLE CCCD: events char (FFF4) notify=%d (prev=%d cur=%d) — app will get "
                         "pair_pending / ble_link_ready / error",
                         s_notify_enabled ? 1 : 0,
                         event->subscribe.prev_notify,
                         event->subscribe.cur_notify);
            } else if (event->subscribe.attr_handle == s_telemetry_notify_handle) {
                s_telemetry_notify_enabled = event->subscribe.cur_notify != 0;
                ESP_LOGI(TAG,
                         "NimBLE CCCD: telemetry char (FFF5) notify=%d (prev=%d cur=%d) — app will get "
                         "evt=telemetry JSON",
                         s_telemetry_notify_enabled ? 1 : 0,
                         event->subscribe.prev_notify,
                         event->subscribe.cur_notify);
            }
            return 0;
        default:
            return 0;
    }
}

static int chr_access(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ble_uuid_cmp(ctxt->chr->uuid, &UUID_INFO.u) == 0 && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char device_id[24] = {0};
        const esp_app_desc_t *app_desc = esp_app_get_description();
        if (device_settings_get_device_id(device_id, sizeof(device_id)) != ESP_OK) {
            (void)strncpy(device_id, "DGPS-UNKNOWN", sizeof(device_id) - 1);
        }
        char payload[160];
        (void)onboarding_protocol_build_device_info(payload, sizeof(payload), device_id, app_desc);
        int rc = os_mbuf_append(ctxt->om, payload, strlen(payload));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ble_uuid_cmp(ctxt->chr->uuid, &UUID_CTRL.u) == 0 &&
        (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR || ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC)) {
        uint16_t pkt_len = OS_MBUF_PKTLEN(ctxt->om);
        uint8_t tmp[256];
        if (pkt_len > sizeof(tmp)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        int rc = ble_hs_mbuf_to_flat(ctxt->om, tmp, sizeof(tmp), NULL);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        for (uint16_t i = 0; i < pkt_len; i++) {
            char c = (char)tmp[i];
            if (c == '\n') {
                s_rx_line_buf[s_rx_line_len] = '\0';
                handle_control_line(s_rx_line_buf);
                s_rx_line_len = 0;
                continue;
            }
            if (s_rx_line_len + 1 < sizeof(s_rx_line_buf)) {
                s_rx_line_buf[s_rx_line_len++] = c;
            } else {
                s_rx_line_len = 0;
                (void)ble_onboarding_gatt_notify_error("invalid_request");
            }
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def GATT_SVCS[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics =
            (struct ble_gatt_chr_def[]) {
                {.uuid = &UUID_INFO.u, .access_cb = chr_access, .flags = BLE_GATT_CHR_F_READ},
                {.uuid = &UUID_CTRL.u,
                 .access_cb = chr_access,
                 .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
                {.uuid = &UUID_NOTIFY.u,
                 .access_cb = chr_access,
                 .flags = BLE_GATT_CHR_F_NOTIFY,
                 .val_handle = &s_notify_handle},
                {.uuid = &UUID_TELEMETRY.u,
                 .access_cb = chr_access,
                 .flags = BLE_GATT_CHR_F_NOTIFY,
                 .val_handle = &s_telemetry_notify_handle},
                {0},
            },
    },
    {0},
};

static void on_sync(void)
{
    (void)ble_onboarding_gatt_start_advertising();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
#endif

esp_err_t ble_onboarding_gatt_init(void)
{
    s_connected = false;
    s_conn_handle = INVALID_CONN_HANDLE;
    s_notify_handle = 0;
    s_telemetry_notify_handle = 0;
    s_rx_line_len = 0;
    s_notify_enabled = false;
    s_telemetry_notify_enabled = false;
#if !CONFIG_BT_ENABLED
    ESP_LOGW(TAG, "Bluetooth disabled in sdkconfig; BLE onboarding running in stub mode");
#elif !CONFIG_BT_NIMBLE_ENABLED
    ESP_LOGW(TAG, "NimBLE not enabled in sdkconfig; BLE onboarding running in stub mode");
#else
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(GATT_SVCS);
    if (rc != 0) {
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(GATT_SVCS);
    if (rc != 0) {
        return ESP_FAIL;
    }
    char name[16];
    char dev_id[24] = {0};
    (void)device_settings_get_device_id(dev_id, sizeof(dev_id));
    (void)snprintf(name, sizeof(name), "DGPS_%.4s", (strlen(dev_id) >= 4) ? &dev_id[strlen(dev_id) - 4] : "0000");
    (void)ble_svc_gap_device_name_set(name);
    ESP_LOGI(TAG, "NimBLE initialized with device name: %s", name);
    nimble_port_freertos_init(host_task);
#endif
    return ESP_OK;
}

esp_err_t ble_onboarding_gatt_start_advertising(void)
{
#if !CONFIG_BT_ENABLED
    ESP_LOGW(TAG, "start_advertising skipped (CONFIG_BT_ENABLED is off)");
#elif !CONFIG_BT_NIMBLE_ENABLED
    ESP_LOGW(TAG, "start_advertising skipped (CONFIG_BT_NIMBLE_ENABLED is off)");
#else
    struct ble_gap_adv_params adv = {0};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&UUID_SVC;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        return ESP_FAIL;
    }
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    if (rc != 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "NimBLE GAP: advertising started (svc FFF0, device discoverable)");
#endif
    return ESP_OK;
}

esp_err_t ble_onboarding_gatt_stop_advertising(void)
{
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
    (void)ble_gap_adv_stop();
#endif
    return ESP_OK;
}

esp_err_t ble_onboarding_gatt_notify_link_ready(void)
{
    char line[256];
    (void)onboarding_protocol_build_link_ready(line, sizeof(line));
    ESP_LOGI(TAG, "Sending ble_link_ready notify");
    return notify_json_line(line);
}

esp_err_t ble_onboarding_gatt_notify_telemetry_json(const char *line)
{
    return notify_json_line_with_handle(line, s_telemetry_notify_handle, s_telemetry_notify_enabled);
}

esp_err_t ble_onboarding_gatt_notify_pair_pending(const char *code4)
{
    if (code4 == NULL || strlen(code4) != 4) {
        return ESP_ERR_INVALID_ARG;
    }
    char line[96];
    (void)onboarding_protocol_build_pair_pending(line, sizeof(line), code4);
    ESP_LOGI(TAG, "Sending pair_pending_confirm notify (code=%s)", code4);
    return notify_json_line(line);
}

esp_err_t ble_onboarding_gatt_notify_error(const char *reason)
{
    char line[128];
    (void)onboarding_protocol_build_error(line, sizeof(line), reason);
    ESP_LOGW(TAG, "Sending error notify (reason=%s)", (reason != NULL) ? reason : "unknown");
    return notify_json_line(line);
}

bool ble_onboarding_gatt_is_connected(void)
{
    return s_connected;
}

bool ble_onboarding_gatt_notify_enabled(void)
{
    return s_notify_enabled;
}

bool ble_onboarding_gatt_telemetry_notify_enabled(void)
{
    return s_telemetry_notify_enabled;
}
