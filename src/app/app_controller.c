#include "app/app_controller.h"

#include "freertos/task.h"
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
    if (mode == POWER_MANAGER_MODE_CHARGING) {
        dashboard_screen_render_charging(app->dashboard, app->battery_percentage);
    } else {
        dashboard_screen_render_operation(app->dashboard, &t);
    }

    app->next_render_tick = now + pdMS_TO_TICKS(250);
}
