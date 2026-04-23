#include "ble/onboarding_protocol.h"

#include <stdio.h>
#include <string.h>

static bool extract_string_value(const char *line, const char *key, char *out, size_t out_len)
{
    if (line == NULL || key == NULL || out == NULL || out_len < 2) {
        return false;
    }

    const char *p = strstr(line, key);
    if (p == NULL) {
        return false;
    }
    p += strlen(key);
    size_t i = 0;
    while (*p != '\0' && *p != '"' && i + 1 < out_len) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0 && *p == '"';
}

bool onboarding_protocol_parse_control_line(const char *line, onboarding_control_cmd_t *out_cmd)
{
    if (line == NULL || out_cmd == NULL) {
        return false;
    }

    memset(out_cmd, 0, sizeof(*out_cmd));
    if (strstr(line, "\"cmd\":\"wifi_connected\"") != NULL) {
        out_cmd->type = ONBOARDING_CMD_WIFI_CONNECTED;
        return true;
    }
    if (strstr(line, "\"cmd\":\"pair_request\"") == NULL) {
        return false;
    }

    char code[8] = {0};
    if (!extract_string_value(line, "\"code\":\"", code, sizeof(code))) {
        return false;
    }
    if (strlen(code) != 4) {
        return false;
    }
    for (size_t i = 0; i < 4; i++) {
        if (code[i] < '0' || code[i] > '9') {
            return false;
        }
    }

    out_cmd->type = ONBOARDING_CMD_PAIR_REQUEST;
    (void)strncpy(out_cmd->code, code, sizeof(out_cmd->code) - 1);
    (void)extract_string_value(line, "\"app_nonce\":\"", out_cmd->app_nonce, sizeof(out_cmd->app_nonce));
    return true;
}

size_t onboarding_protocol_build_device_info(char *out, size_t out_len, const char *device_id,
                                             const esp_app_desc_t *app_desc)
{
    if (out == NULL || out_len < 2) {
        return 0;
    }
    return (size_t)snprintf(out, out_len, "{\"device_id\":\"%s\",\"firmware\":\"%s\"}\n",
                            (device_id != NULL) ? device_id : "DGPS-UNKNOWN",
                            (app_desc != NULL) ? app_desc->version : "unknown");
}

size_t onboarding_protocol_build_pair_pending(char *out, size_t out_len, const char *code4)
{
    if (out == NULL || out_len < 2 || code4 == NULL || strlen(code4) != 4) {
        return 0;
    }
    return (size_t)snprintf(out, out_len, "{\"evt\":\"pair_pending_confirm\",\"code\":\"%s\"}\n", code4);
}

size_t onboarding_protocol_build_softap_ready(char *out, size_t out_len, const wifi_softap_credentials_t *creds)
{
    if (out == NULL || out_len < 2 || creds == NULL) {
        return 0;
    }
    return (size_t)snprintf(out, out_len,
                            "{\"evt\":\"softap_ready\",\"ssid\":\"%s\",\"password\":\"%s\","
                            "\"ip\":\"192.168.4.1\",\"port\":80}\n",
                            creds->ssid, creds->password);
}

size_t onboarding_protocol_build_error(char *out, size_t out_len, const char *reason)
{
    if (out == NULL || out_len < 2) {
        return 0;
    }
    return (size_t)snprintf(out, out_len, "{\"evt\":\"error\",\"reason\":\"%s\"}\n",
                            (reason != NULL) ? reason : "unknown");
}
