#include "telemetry.h"
#include <string.h>

SemaphoreHandle_t telemetry_mutex;

static telemetry_t s_telemetry;

void telemetry_init(void)
{
    telemetry_mutex = xSemaphoreCreateMutex();
    memset(&s_telemetry, 0, sizeof(s_telemetry));
    s_telemetry.battery.valid = false;
    s_telemetry.rtk.valid = false;
}

void telemetry_get_copy(telemetry_t *out)
{
    if (!out) {
        return;
    }
    if (xSemaphoreTake(telemetry_mutex, portMAX_DELAY) == pdTRUE) {
        *out = s_telemetry;
        xSemaphoreGive(telemetry_mutex);
    }
}

void telemetry_set_imu(float yaw_deg, float roll_deg, float pitch_deg, bool valid)
{
    if (xSemaphoreTake(telemetry_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_telemetry.imu_yaw_deg = yaw_deg;
    s_telemetry.imu_roll_deg = roll_deg;
    s_telemetry.imu_pitch_deg = pitch_deg;
    s_telemetry.imu_valid = valid;
    xSemaphoreGive(telemetry_mutex);
}

void telemetry_set_battery(const telemetry_battery_t *b)
{
    if (!b || xSemaphoreTake(telemetry_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_telemetry.battery = *b;
    xSemaphoreGive(telemetry_mutex);
}

void telemetry_set_rtk(const telemetry_rtk_t *rtk)
{
    if (!rtk || xSemaphoreTake(telemetry_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_telemetry.rtk = *rtk;
    xSemaphoreGive(telemetry_mutex);
}

void telemetry_set_rtk_sat_visible(uint8_t num_visible)
{
    if (xSemaphoreTake(telemetry_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_telemetry.rtk.num_sv_visible = num_visible;
    xSemaphoreGive(telemetry_mutex);
}
