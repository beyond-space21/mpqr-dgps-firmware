#include "app/app_controller.h"

#include "freertos/task.h"
#include <stdio.h>
#include "app/onboarding_controller.h"
#include "tasks/power_manager_task.h"
#include "telemetry.h"

void app_controller_init(app_controller_t *app, dashboard_screen_t *dashboard)
{
    app->dashboard = dashboard;
    app->battery_percentage = 0;
    app->next_render_tick = xTaskGetTickCount();
}

void app_controller_update(app_controller_t *app)
{
    const TickType_t now = xTaskGetTickCount();
    if (now < app->next_render_tick) {
        return;
    }

    telemetry_t t;
    telemetry_get_copy(&t);

    if (t.battery.valid) {
        float soc = t.battery.soc_percent;
        if (soc < 0.0f) {
            soc = 0.0f;
        }
        if (soc > 100.0f) {
            soc = 100.0f;
        }
        app->battery_percentage = (uint8_t)soc;
    }

    power_manager_mode_t mode = power_manager_current_mode();
    const onboarding_status_t onboarding = onboarding_controller_get_status();

    if (mode == POWER_MANAGER_MODE_OPERATION &&
        onboarding.state != ONBOARDING_STATE_IDLE &&
        onboarding.state != ONBOARDING_STATE_CONNECTED) {
        char detail[96];
        switch (onboarding.state) {
            case ONBOARDING_STATE_BLE_ADVERTISING:
                dashboard_screen_render_onboarding(app->dashboard, "Device Pairing", "Advertising over BLE");
                break;
            case ONBOARDING_STATE_PAIR_PENDING_CONFIRM:
                (void)snprintf(detail, sizeof(detail), "Confirm code on device: %s", onboarding.code);
                dashboard_screen_render_onboarding(app->dashboard, "Pair Confirmation", detail);
                break;
            case ONBOARDING_STATE_LINK_STARTING:
                dashboard_screen_render_onboarding(app->dashboard, "BLE Link Setup", "Preparing BLE data link");
                break;
            case ONBOARDING_STATE_LINK_READY:
                dashboard_screen_render_onboarding(app->dashboard, "BLE Link Ready", "Telemetry and status over BLE");
                break;
            case ONBOARDING_STATE_FAILED:
                dashboard_screen_render_onboarding(app->dashboard, "Onboarding Failed", "Retry pairing from app");
                break;
            case ONBOARDING_STATE_CONNECTED:
            case ONBOARDING_STATE_IDLE:
            default:
                break;
        }
    } else if (mode == POWER_MANAGER_MODE_OPERATION) {
        dashboard_screen_render_operation(app->dashboard, &t);
    } else if (mode == POWER_MANAGER_MODE_CHARGING) {
        dashboard_screen_render_charging(app->dashboard, app->battery_percentage);
    } else {
        dashboard_screen_render_boot_wait(app->dashboard);
    }

    app->next_render_tick = now + pdMS_TO_TICKS(250);
}
