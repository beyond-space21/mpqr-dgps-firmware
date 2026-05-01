#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/** Last differential snapshot from GNSS UART (rtk_task). @ref source identifies origin. */
typedef struct {
    bool valid;
    /** 1 = UBX-NAV-STATUS (0x01 0x03), 2 = UBX-NAV-PVT (0x01 0x07) flags bit1 diffSoln */
    uint8_t source;
    /** NAV-STATUS: flags bit0. NAV-PVT: flags bit1 (same semantic: diff corrections used). */
    bool diff_soln;
    /** NAV-STATUS: gpsFix byte. NAV-PVT: fixType. */
    uint8_t gps_fix;
    uint32_t itow_ms;
} telemetry_nav_status_t;

/** RTK / GNSS snapshot (written by rtk_task, read by display / network). */
typedef struct {
    bool valid;
    uint8_t fix_type;
    /** NMEA GGA-style quality 0–5 (from NAV-PVT); drives WebSocket RTK_fix_status. */
    uint8_t fix_quality_code;
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
    char gga[160];
} telemetry_rtk_t;

/** BQ27441 snapshot (written by fuel_gauge_task). */
typedef struct {
    bool valid;
    /** 0–100 % when valid */
    float soc_percent;
    uint16_t voltage_mv;
    int16_t current_ma;
} telemetry_battery_t;

typedef enum {
    TELEMETRY_WIFI_OFF = 0,
    TELEMETRY_WIFI_CONNECTING,
    TELEMETRY_WIFI_CONNECTED,
    TELEMETRY_WIFI_FAILED,
} telemetry_wifi_state_t;

typedef struct {
    telemetry_wifi_state_t state;
    char ssid[33];
    char ip[16];
} telemetry_wifi_t;

typedef enum {
    TELEMETRY_NTRIP_DISCONNECTED = 0,
    TELEMETRY_NTRIP_CONNECTING,
    TELEMETRY_NTRIP_STREAMING,
    TELEMETRY_NTRIP_AUTH_FAILED,
    TELEMETRY_NTRIP_ERROR,
} telemetry_ntrip_state_t;

typedef struct {
    telemetry_ntrip_state_t state;
    char last_error[64];
} telemetry_ntrip_t;

/** Central sensor / state store; protected by @ref telemetry_mutex */
typedef struct {
    float imu_yaw_deg;
    float imu_roll_deg;
    float imu_pitch_deg;
    bool imu_valid;

    telemetry_battery_t battery;

    telemetry_rtk_t rtk;
    telemetry_nav_status_t nav_status;
    telemetry_wifi_t wifi;
    telemetry_ntrip_t ntrip;
} telemetry_t;

extern SemaphoreHandle_t telemetry_mutex;

void telemetry_init(void);

/** GGA-style @ref telemetry_rtk_t.fix_quality_code → same labels as BLE `RTK_fix_status`. */
const char *telemetry_rtk_quality_str(const telemetry_rtk_t *rtk);

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

/** RTK task: last UBX-NAV-STATUS (diffSoln, gpsFix, iTOW). */
void telemetry_set_nav_status(const telemetry_nav_status_t *ns);

void telemetry_set_wifi(const telemetry_wifi_t *wifi);
void telemetry_set_ntrip(const telemetry_ntrip_t *ntrip);

#endif
