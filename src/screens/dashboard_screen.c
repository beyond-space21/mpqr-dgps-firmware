#include "screens/dashboard_screen.h"

#include <stdio.h>
#include <string.h>
#include "app/onboarding_controller.h"
#include "esp_log.h"
#include "lvgl.h"
#include "widgets/battery_widget.h"
#include "widgets/operation_log_widget.h"

struct dashboard_screen {
    battery_widget_t *battery;
    operation_log_widget_t *operation_log;
    lv_obj_t *operation_button;
    lv_obj_t *operation_button_label;
};

static const char *TAG = "dashboard_ui";

static void operation_button_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (onboarding_controller_is_waiting_user_confirm()) {
        ESP_LOGI(TAG, "User pressed CONFIRM for pairing code");
        onboarding_controller_confirm_pairing(true);
    }
}

void dashboard_screen_render_boot_wait(dashboard_screen_t *screen)
{
    if (!screen) {
        return;
    }
    battery_widget_set_visible(screen->battery, false);
    operation_log_widget_set_visible(screen->operation_log, false);
    lv_obj_add_flag(screen->operation_button, LV_OBJ_FLAG_HIDDEN);
}

void dashboard_screen_render_onboarding(dashboard_screen_t *screen, const char *title, const char *detail)
{
    if (!screen) {
        return;
    }

    char text[320];
    (void)snprintf(text, sizeof(text), "%s\n\n%s",
                   (title != NULL) ? title : "Onboarding",
                   (detail != NULL) ? detail : "Waiting");

    battery_widget_set_visible(screen->battery, false);
    operation_log_widget_set_visible(screen->operation_log, true);
    if (title != NULL && strcmp(title, "Pair Confirmation") == 0) {
        lv_label_set_text(screen->operation_button_label, "CONFIRM");
        lv_obj_clear_flag(screen->operation_button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen->operation_button, LV_OBJ_FLAG_HIDDEN);
    }
    operation_log_widget_render(screen->operation_log, text);
}

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
    screen->operation_button = lv_btn_create(scr);
    if (!screen->battery || !screen->operation_log || !screen->operation_button) {
        lv_mem_free(screen);
        return NULL;
    }

    lv_obj_set_size(screen->operation_button, 140, 56);
    lv_obj_align(screen->operation_button, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_radius(screen->operation_button, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen->operation_button, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen->operation_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen->operation_button, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(screen->operation_button, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    screen->operation_button_label = lv_label_create(screen->operation_button);
    lv_label_set_text(screen->operation_button_label, "ACTION");
    lv_obj_center(screen->operation_button_label);
    lv_obj_add_event_cb(screen->operation_button, operation_button_cb, LV_EVENT_CLICKED, NULL);

    battery_widget_set_visible(screen->battery, false);
    operation_log_widget_set_visible(screen->operation_log, false);
    lv_obj_add_flag(screen->operation_button, LV_OBJ_FLAG_HIDDEN);
    return screen;
}

void dashboard_screen_render_charging(dashboard_screen_t *screen, uint8_t battery_percentage)
{
    if (!screen) {
        return;
    }
    operation_log_widget_set_visible(screen->operation_log, false);
    battery_widget_set_visible(screen->battery, true);
    lv_obj_add_flag(screen->operation_button, LV_OBJ_FLAG_HIDDEN);
    battery_widget_render(screen->battery, battery_percentage);
}

void dashboard_screen_render_operation(dashboard_screen_t *screen, const telemetry_t *telemetry)
{
    if (!screen || !telemetry) {
        return;
    }

    char text[320];
    snprintf(text, sizeof(text),
             "IMU valid: %s\nYaw: %.1f  Roll: %.1f  Pitch: %.1f\n\nBattery valid: %s\nSOC: %.1f%%  Voltage: %u mV\nCurrent: %d mA\n\nRTK valid: %s\nFix: %s  SV: %u/%u\nLat: %.7f\nLon: %.7f",
             telemetry->imu_valid ? "yes" : "no", (double)telemetry->imu_yaw_deg,
             (double)telemetry->imu_roll_deg, (double)telemetry->imu_pitch_deg,
             telemetry->battery.valid ? "yes" : "no", (double)telemetry->battery.soc_percent,
             (unsigned)telemetry->battery.voltage_mv, (int)telemetry->battery.current_ma, telemetry->rtk.valid ? "yes" : "no",
             telemetry_rtk_quality_str(&telemetry->rtk), (unsigned)telemetry->rtk.num_sv,
             (unsigned)telemetry->rtk.num_sv_visible, (double)telemetry->rtk.lat_deg,
             (double)telemetry->rtk.lon_deg);

    battery_widget_set_visible(screen->battery, false);
    operation_log_widget_set_visible(screen->operation_log, true);
    lv_obj_clear_flag(screen->operation_button, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(screen->operation_button_label, "ACTION");
    operation_log_widget_render(screen->operation_log, text);
}
