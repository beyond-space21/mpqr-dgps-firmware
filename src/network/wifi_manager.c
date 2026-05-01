#include "network/wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "telemetry.h"

static wifi_manager_state_t s_state = WIFI_MANAGER_STATE_OFF;
static bool s_netif_ready;
static bool s_handlers_registered;
static esp_netif_t *s_sta_netif;
static char s_ip[16];
static esp_event_handler_instance_t s_wifi_evt_inst;
static esp_event_handler_instance_t s_ip_evt_inst;
static const char *TAG = "wifi_manager";
static wifi_manager_state_t s_logged_wifi_fsm = WIFI_MANAGER_STATE_OFF;
static bool s_wifi_fsm_log_inited;

static const char *wifi_line_state(wifi_manager_state_t st)
{
    switch (st) {
        case WIFI_MANAGER_STATE_CONNECTING:
            return "connecting";
        case WIFI_MANAGER_STATE_CONNECTED:
            return "connected";
        case WIFI_MANAGER_STATE_FAILED:
            return "failed";
        case WIFI_MANAGER_STATE_OFF:
        default:
            return "off";
    }
}

static void publish_wifi_status(void)
{
    telemetry_wifi_t t = {
        .state = TELEMETRY_WIFI_OFF,
    };
    switch (s_state) {
        case WIFI_MANAGER_STATE_CONNECTING:
            t.state = TELEMETRY_WIFI_CONNECTING;
            break;
        case WIFI_MANAGER_STATE_CONNECTED:
            t.state = TELEMETRY_WIFI_CONNECTED;
            break;
        case WIFI_MANAGER_STATE_FAILED:
            t.state = TELEMETRY_WIFI_FAILED;
            break;
        case WIFI_MANAGER_STATE_OFF:
        default:
            t.state = TELEMETRY_WIFI_OFF;
            break;
    }
    (void)strncpy(t.ssid, CFG_WIFI_STA_SSID, sizeof(t.ssid) - 1);
    (void)strncpy(t.ip, s_ip, sizeof(t.ip) - 1);
    telemetry_set_wifi(&t);

    if (!s_wifi_fsm_log_inited || s_state != s_logged_wifi_fsm) {
        s_wifi_fsm_log_inited = true;
        s_logged_wifi_fsm = s_state;
        if (s_state == WIFI_MANAGER_STATE_CONNECTED) {
            ESP_LOGI(TAG, "wifi: connected (ip=%s)", s_ip);
        } else {
            ESP_LOGI(TAG, "wifi: %s", wifi_line_state(s_state));
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            s_state = WIFI_MANAGER_STATE_CONNECTING;
            publish_wifi_status();
            (void)esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_state = WIFI_MANAGER_STATE_CONNECTING;
            (void)snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
            publish_wifi_status();
            (void)esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)event_data;
        if (evt != NULL) {
            (void)snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        }
        s_state = WIFI_MANAGER_STATE_CONNECTED;
        publish_wifi_status();
    }
}

esp_err_t wifi_manager_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (!s_netif_ready) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            return ESP_FAIL;
        }
        s_netif_ready = true;
    }

    if (!s_handlers_registered) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_wifi_evt_inst));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &s_ip_evt_inst));
        s_handlers_registered = true;
    }

    s_state = WIFI_MANAGER_STATE_OFF;
    (void)snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
    publish_wifi_status();
    return ESP_OK;
}

esp_err_t wifi_manager_start_sta(void)
{
    if (s_state == WIFI_MANAGER_STATE_CONNECTED || s_state == WIFI_MANAGER_STATE_CONNECTING) {
        ESP_LOGI(TAG, "Wi-Fi STA already active");
        return ESP_OK;
    }

    s_state = WIFI_MANAGER_STATE_CONNECTING;
    publish_wifi_status();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        s_state = WIFI_MANAGER_STATE_FAILED;
        return err;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        s_state = WIFI_MANAGER_STATE_FAILED;
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        s_state = WIFI_MANAGER_STATE_FAILED;
        publish_wifi_status();
        return err;
    }

    wifi_config_t sta_cfg = {0};
    (void)strncpy((char *)sta_cfg.sta.ssid, CFG_WIFI_STA_SSID, sizeof(sta_cfg.sta.ssid) - 1);
    (void)strncpy((char *)sta_cfg.sta.password, CFG_WIFI_STA_PASSWORD, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
        s_state = WIFI_MANAGER_STATE_FAILED;
        publish_wifi_status();
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        s_state = WIFI_MANAGER_STATE_FAILED;
        publish_wifi_status();
        return err;
    }
    return ESP_OK;
}

esp_err_t wifi_manager_stop_sta(void)
{
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    s_state = WIFI_MANAGER_STATE_OFF;
    (void)snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
    publish_wifi_status();
    return ESP_OK;
}

wifi_manager_state_t wifi_manager_get_state(void)
{
    return s_state;
}

bool wifi_manager_is_connected(void)
{
    return s_state == WIFI_MANAGER_STATE_CONNECTED;
}

const char *wifi_manager_get_ssid(void)
{
    return CFG_WIFI_STA_SSID;
}

const char *wifi_manager_get_ip(void)
{
    return s_ip;
}
