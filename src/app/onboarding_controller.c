#include "app/onboarding_controller.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static onboarding_status_t s_status;
static SemaphoreHandle_t s_lock;
static onboarding_confirm_deadline_t s_confirm_deadline;
static const char *TAG = "onboard_ctrl";

static void lock_init_once(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
}

static void status_lock(void)
{
    if (s_lock != NULL) {
        (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void status_unlock(void)
{
    if (s_lock != NULL) {
        (void)xSemaphoreGive(s_lock);
    }
}

void onboarding_controller_init(void)
{
    lock_init_once();
    status_lock();
    memset(&s_status, 0, sizeof(s_status));
    memset(&s_confirm_deadline, 0, sizeof(s_confirm_deadline));
    s_status.state = ONBOARDING_STATE_IDLE;
    status_unlock();
}

void onboarding_controller_start(void)
{
    status_lock();
    s_status.state = ONBOARDING_STATE_BLE_ADVERTISING;
    s_status.requires_user_confirm = false;
    memset(&s_confirm_deadline, 0, sizeof(s_confirm_deadline));
    memset(s_status.code, 0, sizeof(s_status.code));
    ESP_LOGI(TAG, "State -> BLE_ADVERTISING");
    status_unlock();
}

void onboarding_controller_set_pair_code(const char *code4)
{
    status_lock();
    s_status.state = ONBOARDING_STATE_PAIR_PENDING_CONFIRM;
    s_status.requires_user_confirm = true;
    s_confirm_deadline.active = true;
    s_confirm_deadline.started_at = xTaskGetTickCount();
    s_confirm_deadline.timeout_ticks = pdMS_TO_TICKS(90000);
    memset(s_status.code, 0, sizeof(s_status.code));
    if (code4 != NULL) {
        (void)strncpy(s_status.code, code4, sizeof(s_status.code) - 1);
    }
    ESP_LOGI(TAG, "State -> PAIR_PENDING_CONFIRM (code=%s, timeout=90s)", s_status.code);
    status_unlock();
}

void onboarding_controller_confirm_pairing(bool accepted)
{
    status_lock();
    s_status.requires_user_confirm = false;
    memset(&s_confirm_deadline, 0, sizeof(s_confirm_deadline));
    s_status.state = accepted ? ONBOARDING_STATE_SOFTAP_STARTING : ONBOARDING_STATE_FAILED;
    ESP_LOGI(TAG, "Pair confirmation result: %s -> state=%s",
             accepted ? "accepted" : "rejected",
             accepted ? "SOFTAP_STARTING" : "FAILED");
    status_unlock();
}

void onboarding_controller_mark_softap_ready(void)
{
    status_lock();
    s_status.state = ONBOARDING_STATE_SOFTAP_READY;
    ESP_LOGI(TAG, "State -> SOFTAP_READY");
    status_unlock();
}

void onboarding_controller_mark_connected(void)
{
    status_lock();
    s_status.state = ONBOARDING_STATE_CONNECTED;
    ESP_LOGI(TAG, "State -> CONNECTED (app acknowledged wifi)");
    status_unlock();
}

void onboarding_controller_mark_failed(void)
{
    status_lock();
    s_status.state = ONBOARDING_STATE_FAILED;
    ESP_LOGW(TAG, "State -> FAILED");
    status_unlock();
}

onboarding_status_t onboarding_controller_get_status(void)
{
    onboarding_status_t out;
    status_lock();
    out = s_status;
    status_unlock();
    return out;
}

onboarding_confirm_deadline_t onboarding_controller_get_confirm_deadline(void)
{
    onboarding_confirm_deadline_t out = {0};
    status_lock();
    out = s_confirm_deadline;
    status_unlock();
    return out;
}

bool onboarding_controller_is_waiting_user_confirm(void)
{
    bool waiting = false;
    status_lock();
    waiting = s_status.state == ONBOARDING_STATE_PAIR_PENDING_CONFIRM && s_status.requires_user_confirm;
    status_unlock();
    return waiting;
}
