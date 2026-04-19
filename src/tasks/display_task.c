#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display_task.h"
#include "system_fsm.h"
#include "telemetry.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

static const char *TAG = "display_task";

/**
 * In CHARGING mode only the display task is expected to run frequently; use light sleep
 * between refreshes with both timer and button wake sources so the 2 s long-press path
 * remains responsive without busy-waiting.
 */
static void light_sleep_until_next_tick_ms(uint32_t period_ms)
{
    if (period_ms == 0u) {
        return;
    }

    /* Timer wakeup always armed so we return periodically even if the line idles high. */
    esp_err_t err = esp_sleep_enable_timer_wakeup((uint64_t)period_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_sleep_enable_timer_wakeup failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        return;
    }

    /* Early wake if the user presses the button (active low). */
    bool gpio_wake_configured = false;
    err = gpio_wakeup_enable((gpio_num_t)CFG_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK) {
        /* IDF 5.x: per-pin levels are configured via gpio_wakeup_enable(); this enables the source. */
        err = esp_sleep_enable_gpio_wakeup();
        gpio_wake_configured = (err == ESP_OK);
    }
    if (!gpio_wake_configured) {
        ESP_LOGD(TAG, "GPIO wake not enabled (%s); timer-only light sleep", esp_err_to_name(err));
    }

    esp_light_sleep_start();

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (gpio_wake_configured) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
        (void)gpio_wakeup_disable((gpio_num_t)CFG_BUTTON_GPIO);
    }
}

void display_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Display task — UI placeholder (telemetry + run-mode aware timing)");

    bool have_last_mode = false;
    app_run_mode_t last_mode = APP_RUN_MODE_OPERATION;

    while (1) {
        telemetry_t t;
        telemetry_get_copy(&t);

        if (t.imu_valid) {
            ESP_LOGI(TAG, "IMU: yaw=%.1f roll=%.1f pitch=%.1f", t.imu_yaw_deg, t.imu_roll_deg,
                     t.imu_pitch_deg);
        }

        if (t.battery.valid) {
            ESP_LOGI(TAG, "BAT: SOC=%.1f%% %u mV", (double)t.battery.soc_percent,
                     (unsigned)t.battery.voltage_mv);
        }

        if (t.rtk.valid) {
            const telemetry_rtk_t *g = &t.rtk;
            ESP_LOGI(TAG,
                     "RTK: SAT=%u PVT fix=%u sv=%u %04u-%02u-%02u %02u:%02u:%02u lat=%.7f lon=%.7f "
                     "hMSL=%.3fm hAcc=%.2fm vAcc=%.2fm gSpd=%.2fm/s head=%.2fdeg "
                     "vNED=%.2f,%.2f,%.2f m/s pDOP=%.2f",
                     (unsigned)g->num_sv_visible, (unsigned)g->fix_type, (unsigned)g->num_sv,
                     (unsigned)g->year, (unsigned)g->month, (unsigned)g->day, (unsigned)g->hour,
                     (unsigned)g->min, (unsigned)g->sec, (double)g->lat_deg, (double)g->lon_deg,
                     (double)g->h_msl_m, (double)g->h_acc_m, (double)g->v_acc_m,
                     (double)g->g_speed_m_s, (double)g->heading_deg, (double)g->vel_n,
                     (double)g->vel_e, (double)g->vel_d, (double)g->pdop);
        }

        app_run_mode_t mode = system_fsm_get_mode();
        if (!have_last_mode || mode != last_mode) {
            have_last_mode = true;
            last_mode = mode;
            ESP_LOGI(TAG, "run mode → %d (0=boot_wait 1=charging 2=operation)", (int)mode);
        }

        uint32_t period_ms;

        if (system_fsm_display_use_low_power_refresh()) {
            period_ms = CFG_DISPLAY_CHARGING_PERIOD_MS;
            light_sleep_until_next_tick_ms(period_ms);
        } else if (mode == APP_RUN_MODE_BOOT_AWAIT_LONG_PRESS) {
            period_ms = CFG_DISPLAY_BOOT_AWAIT_PERIOD_MS;
            vTaskDelay(pdMS_TO_TICKS(period_ms));
        } else {
            period_ms = CFG_DISPLAY_OPERATION_PERIOD_MS;
            vTaskDelay(pdMS_TO_TICKS(period_ms));
        }
    }
}
