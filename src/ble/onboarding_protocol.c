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

size_t onboarding_protocol_build_link_ready(char *out, size_t out_len)
{
    if (out == NULL || out_len < 2) {
        return 0;
    }
    return (size_t)snprintf(out, out_len, "{\"evt\":\"ble_link_ready\"}\n");
}

size_t onboarding_protocol_build_error(char *out, size_t out_len, const char *reason)
{
    if (out == NULL || out_len < 2) {
        return 0;
    }
    return (size_t)snprintf(out, out_len, "{\"evt\":\"error\",\"reason\":\"%s\"}\n",
                            (reason != NULL) ? reason : "unknown");
}

/** Escape UTF-8 text for embedding in a JSON string literal (output excludes outer quotes). */
static void json_escape_string(const char *in, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return;
    }
    if (in == NULL) {
        in = "";
    }
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 4 < out_cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (c < 0x20u) {
            out[j++] = ' ';
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

static void emit_str_field(onboarding_telemetry_emit_fn emit, char *buf, size_t buf_len, const char *key,
                           const char *raw)
{
    char esc[192];
    json_escape_string((raw != NULL) ? raw : "", esc, sizeof(esc));
    (void)snprintf(buf, buf_len, "{\"evt\":\"telemetry\",\"%s\":\"%s\"}\n", key, esc);
    emit(buf);
}

void onboarding_protocol_dispatch_telemetry(onboarding_telemetry_emit_fn emit, const char *rtk_fix_status,
                                            float h_acc, float v_acc, float lat, float lng,
                                            float height_m, unsigned satellites,
                                            const char *wifi_status, const char *wifi_ssid,
                                            const char *wifi_ip, const char *ntrip_status,
                                            const char *ntrip_error)
{
    if (emit == NULL) {
        return;
    }
    char buf[288];

    (void)snprintf(buf, sizeof(buf), "{\"evt\":\"telemetry\",\"horizontal_accuracy\":%.3f}\n", (double)h_acc);
    emit(buf);
    (void)snprintf(buf, sizeof(buf), "{\"evt\":\"telemetry\",\"vertical_accuracy\":%.3f}\n", (double)v_acc);
    emit(buf);
    emit_str_field(emit, buf, sizeof(buf), "RTK_fix_status",
                   (rtk_fix_status != NULL) ? rtk_fix_status : "No fix");
    (void)snprintf(buf, sizeof(buf), "{\"evt\":\"telemetry\",\"lat\":%.7f}\n", (double)lat);
    emit(buf);
    (void)snprintf(buf, sizeof(buf), "{\"evt\":\"telemetry\",\"lng\":%.7f}\n", (double)lng);
    emit(buf);
    (void)snprintf(buf, sizeof(buf), "{\"evt\":\"telemetry\",\"height_m\":%.3f}\n", (double)height_m);
    emit(buf);
    (void)snprintf(buf, sizeof(buf), "{\"evt\":\"telemetry\",\"satellites\":%u}\n", satellites);
    emit(buf);
    emit_str_field(emit, buf, sizeof(buf), "wifi_status", (wifi_status != NULL) ? wifi_status : "off");
    emit_str_field(emit, buf, sizeof(buf), "wifi_ssid", (wifi_ssid != NULL) ? wifi_ssid : "");
    emit_str_field(emit, buf, sizeof(buf), "wifi_ip", (wifi_ip != NULL) ? wifi_ip : "0.0.0.0");
    emit_str_field(emit, buf, sizeof(buf), "ntrip_status", (ntrip_status != NULL) ? ntrip_status : "disconnected");
    emit_str_field(emit, buf, sizeof(buf), "ntrip_last_error", (ntrip_error != NULL) ? ntrip_error : "");
}
