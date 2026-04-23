#ifndef ONBOARDING_CONTROLLER_H
#define ONBOARDING_CONTROLLER_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"

typedef enum {
    ONBOARDING_STATE_IDLE = 0,
    ONBOARDING_STATE_BLE_ADVERTISING,
    ONBOARDING_STATE_PAIR_PENDING_CONFIRM,
    ONBOARDING_STATE_SOFTAP_STARTING,
    ONBOARDING_STATE_SOFTAP_READY,
    ONBOARDING_STATE_CONNECTED,
    ONBOARDING_STATE_FAILED,
} onboarding_state_t;

typedef struct {
    onboarding_state_t state;
    bool requires_user_confirm;
    char code[5];
} onboarding_status_t;

typedef struct {
    bool active;
    TickType_t started_at;
    TickType_t timeout_ticks;
} onboarding_confirm_deadline_t;

void onboarding_controller_init(void);
void onboarding_controller_start(void);
void onboarding_controller_set_pair_code(const char *code4);
void onboarding_controller_confirm_pairing(bool accepted);
void onboarding_controller_mark_softap_ready(void);
void onboarding_controller_mark_connected(void);
void onboarding_controller_mark_failed(void);
onboarding_status_t onboarding_controller_get_status(void);
onboarding_confirm_deadline_t onboarding_controller_get_confirm_deadline(void);
bool onboarding_controller_is_waiting_user_confirm(void);

#endif
