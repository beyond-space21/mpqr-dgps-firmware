#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display_task.h"
#include "telemetry.h"
#include "esp_log.h"

static const char *TAG = "display_task";

void display_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Display task — QSPI OLED UI placeholder (reads telemetry only)");

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

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
