#ifndef ONBOARDING_PROTOCOL_H
#define ONBOARDING_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_app_desc.h"

typedef enum {
    ONBOARDING_CMD_INVALID = 0,
    ONBOARDING_CMD_PAIR_REQUEST,
} onboarding_cmd_type_t;

typedef struct {
    onboarding_cmd_type_t type;
    char code[5];
    char app_nonce[33];
} onboarding_control_cmd_t;

bool onboarding_protocol_parse_control_line(const char *line, onboarding_control_cmd_t *out_cmd);
size_t onboarding_protocol_build_device_info(char *out, size_t out_len, const char *device_id,
                                             const esp_app_desc_t *app_desc);
size_t onboarding_protocol_build_pair_pending(char *out, size_t out_len, const char *code4);
size_t onboarding_protocol_build_link_ready(char *out, size_t out_len);
size_t onboarding_protocol_build_error(char *out, size_t out_len, const char *reason);

/** One JSON line per call (newline-terminated); use for successive BLE notifies. */
typedef void (*onboarding_telemetry_emit_fn)(const char *line);

void onboarding_protocol_dispatch_telemetry(
    onboarding_telemetry_emit_fn emit, const char *rtk_fix_status, float h_acc, float v_acc, float lat,
    float lng, float height_m, unsigned satellites, const char *wifi_status, const char *wifi_ssid,
    const char *wifi_ip, const char *ntrip_status, const char *ntrip_error);

#endif
