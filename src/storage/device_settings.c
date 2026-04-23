#include "storage/device_settings.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SETTINGS_NS "dev_cfg"
#define KEY_PROVISIONED "provisioned"
#define KEY_DEVICE_ID "device_id"

static esp_err_t ensure_device_id(void)
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(SETTINGS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    char id[24];
    size_t len = sizeof(id);
    err = nvs_get_str(h, KEY_DEVICE_ID, id, &len);
    if (err == ESP_OK) {
        nvs_close(h);
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return err;
    }

    const uint32_t rnd = esp_random();
    (void)snprintf(id, sizeof(id), "DGPS-%08" PRIX32, rnd);
    err = nvs_set_str(h, KEY_DEVICE_ID, id);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t device_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    return ensure_device_id();
}

esp_err_t device_settings_set_provisioned(bool provisioned)
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(SETTINGS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, KEY_PROVISIONED, provisioned ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

bool device_settings_is_provisioned(void)
{
    nvs_handle_t h = 0;
    uint8_t v = 0;
    esp_err_t err = nvs_open(SETTINGS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return false;
    }
    err = nvs_get_u8(h, KEY_PROVISIONED, &v);
    nvs_close(h);
    return (err == ESP_OK) && (v != 0);
}

esp_err_t device_settings_get_device_id(char *out, size_t out_len)
{
    if (out == NULL || out_len < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(SETTINGS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = out_len;
    err = nvs_get_str(h, KEY_DEVICE_ID, out, &len);
    nvs_close(h);
    return err;
}
