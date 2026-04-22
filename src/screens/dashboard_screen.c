#include "screens/dashboard_screen.h"

#include <stdio.h>
#include "lvgl.h"
#include "widgets/battery_widget.h"
#include "widgets/operation_log_widget.h"

struct dashboard_screen {
    battery_widget_t *battery;
    operation_log_widget_t *operation_log;
};

dashboard_screen_t *dashboard_screen_create(void)
{
    dashboard_screen_t *screen = lv_mem_alloc(sizeof(dashboard_screen_t));
    if (!screen) {
        return NULL;
    }

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    screen->battery = battery_widget_create(scr);
    screen->operation_log = operation_log_widget_create(scr);
    if (!screen->battery || !screen->operation_log) {
        lv_mem_free(screen);
        return NULL;
    }

    battery_widget_set_visible(screen->battery, false);
    operation_log_widget_set_visible(screen->operation_log, false);
    return screen;
}

void dashboard_screen_render_charging(dashboard_screen_t *screen, uint8_t battery_percentage)
{
    if (!screen) {
        return;
    }
    operation_log_widget_set_visible(screen->operation_log, false);
    battery_widget_set_visible(screen->battery, true);
    battery_widget_render(screen->battery, battery_percentage);
}

void dashboard_screen_render_operation(dashboard_screen_t *screen, const telemetry_t *telemetry)
{
    if (!screen || !telemetry) {
        return;
    }

    char text[320];
    snprintf(text, sizeof(text),
             "IMU valid: %s\nYaw: %.1f  Roll: %.1f  Pitch: %.1f\n\nBattery valid: %s\nSOC: %.1f%%  Voltage: %u mV\n\nRTK valid: %s\nFix: %u  SV: %u/%u\nLat: %.7f\nLon: %.7f",
             telemetry->imu_valid ? "yes" : "no", (double)telemetry->imu_yaw_deg,
             (double)telemetry->imu_roll_deg, (double)telemetry->imu_pitch_deg,
             telemetry->battery.valid ? "yes" : "no", (double)telemetry->battery.soc_percent,
             (unsigned)telemetry->battery.voltage_mv, telemetry->rtk.valid ? "yes" : "no",
             (unsigned)telemetry->rtk.fix_type, (unsigned)telemetry->rtk.num_sv,
             (unsigned)telemetry->rtk.num_sv_visible, (double)telemetry->rtk.lat_deg,
             (double)telemetry->rtk.lon_deg);

    battery_widget_set_visible(screen->battery, false);
    operation_log_widget_set_visible(screen->operation_log, true);
    operation_log_widget_render(screen->operation_log, text);
}
