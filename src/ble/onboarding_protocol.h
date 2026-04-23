#ifndef ONBOARDING_PROTOCOL_H
#define ONBOARDING_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_app_desc.h"
#include "network/wifi_manager.h"

typedef enum {
    ONBOARDING_CMD_INVALID = 0,
    ONBOARDING_CMD_PAIR_REQUEST,
    ONBOARDING_CMD_WIFI_CONNECTED,
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
size_t onboarding_protocol_build_softap_ready(char *out, size_t out_len, const wifi_softap_credentials_t *creds);
size_t onboarding_protocol_build_error(char *out, size_t out_len, const char *reason);

#endif
