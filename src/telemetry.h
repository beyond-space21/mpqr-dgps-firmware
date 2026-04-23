#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/** RTK / GNSS snapshot (written by rtk_task, read by display / network). */
typedef struct {
    bool valid;
    uint8_t fix_type;
    uint8_t num_sv;
    /** Visible satellites from NAV-SAT if received; 0 if unknown */
    uint8_t num_sv_visible;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    float lat_deg;
    float lon_deg;
    float h_msl_m;
    float h_acc_m;
    float v_acc_m;
    float g_speed_m_s;
    float heading_deg;
    float vel_n;
    float vel_e;
    float vel_d;
    float pdop;
} telemetry_rtk_t;

/** BQ27441 snapshot (written by fuel_gauge_task). */
typedef struct {
    bool valid;
    /** 0–100 % when valid */
    float soc_percent;
    uint16_t voltage_mv;
    int16_t current_ma;
} telemetry_battery_t;

/** Central sensor / state store; protected by @ref telemetry_mutex */
typedef struct {
    float imu_yaw_deg;
    float imu_roll_deg;
    float imu_pitch_deg;
    bool imu_valid;

    telemetry_battery_t battery;

    telemetry_rtk_t rtk;
} telemetry_t;

extern SemaphoreHandle_t telemetry_mutex;

void telemetry_init(void);

/** Copy current telemetry under mutex (safe from any task). */
void telemetry_get_copy(telemetry_t *out);

/** Gyro task: publish IMU fusion angles (degrees). */
void telemetry_set_imu(float yaw_deg, float roll_deg, float pitch_deg, bool valid);

/** Fuel gauge task: publish BQ27441 readings. */
void telemetry_set_battery(const telemetry_battery_t *b);

/** RTK task: publish GNSS snapshot (UART only; uses telemetry_mutex). */
void telemetry_set_rtk(const telemetry_rtk_t *rtk);

/** RTK task: update visible satellite count from NAV-SAT without touching other fields. */
void telemetry_set_rtk_sat_visible(uint8_t num_visible);

#endif
