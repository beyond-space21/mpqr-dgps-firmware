#include "network/wifi_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"

static wifi_manager_state_t s_state = WIFI_MANAGER_STATE_OFF;
static wifi_softap_credentials_t s_last_creds;
static bool s_netif_ready;
static esp_netif_t *s_ap_netif;
static const char *TAG = "wifi_manager";

static void make_temp_credentials(wifi_softap_credentials_t *out_creds)
{
    const uint32_t token = esp_random();
    const uint32_t token_b = esp_random();
    (void)snprintf(out_creds->ssid, sizeof(out_creds->ssid), "DGPS_%04" PRIX32, token & 0xFFFFu);
    (void)snprintf(out_creds->password, sizeof(out_creds->password), "P%08" PRIX32 "%04" PRIX32,
                   token, token_b & 0xFFFFu);
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
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            return ESP_FAIL;
        }
        s_netif_ready = true;
    }

    s_state = WIFI_MANAGER_STATE_OFF;
    memset(&s_last_creds, 0, sizeof(s_last_creds));
    return ESP_OK;
}

esp_err_t wifi_manager_start_softap(wifi_softap_credentials_t *out_creds)
{
    if (out_creds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_state = WIFI_MANAGER_STATE_SOFTAP_STARTING;
    make_temp_credentials(&s_last_creds);
    ESP_LOGI(TAG, "Starting SoftAP with temporary credentials");

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
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        s_state = WIFI_MANAGER_STATE_FAILED;
        return err;
    }

    wifi_config_t ap_cfg = {0};
    (void)strncpy((char *)ap_cfg.ap.ssid, s_last_creds.ssid, sizeof(ap_cfg.ap.ssid) - 1);
    (void)strncpy((char *)ap_cfg.ap.password, s_last_creds.password, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(s_last_creds.ssid);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.pmf_cfg.required = true;
    ap_cfg.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        s_state = WIFI_MANAGER_STATE_FAILED;
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        s_state = WIFI_MANAGER_STATE_FAILED;
        return err;
    }

    *out_creds = s_last_creds;
    s_state = WIFI_MANAGER_STATE_SOFTAP_READY;
    ESP_LOGI(TAG, "SoftAP started: ssid=%s, ip=192.168.4.1", s_last_creds.ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_stop_softap(void)
{
    ESP_LOGI(TAG, "Stopping SoftAP");
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    s_state = WIFI_MANAGER_STATE_OFF;
    return ESP_OK;
}

wifi_manager_state_t wifi_manager_get_state(void)
{
    return s_state;
}

bool wifi_manager_is_ready(void)
{
    return s_state == WIFI_MANAGER_STATE_SOFTAP_READY;
}

wifi_softap_credentials_t wifi_manager_last_credentials(void)
{
    return s_last_creds;
}
