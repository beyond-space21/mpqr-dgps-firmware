#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtk_task.h"
#include "config.h"
#include "telemetry.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "rtk_task";

static uint16_t rd_u16_le(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd_u32_le(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int32_t rd_i32_le(const uint8_t *p) { return (int32_t)rd_u32_le(p); }

static void ubx_ck_update(uint8_t b, uint8_t *ck_a, uint8_t *ck_b)
{
    *ck_a = (uint8_t)(*ck_a + b);
    *ck_b = (uint8_t)(*ck_b + *ck_a);
}

static uint8_t nmea_checksum_body(const char *body)
{
    uint8_t ck = 0;
    while (*body) {
        ck ^= (uint8_t)(*body++);
    }
    return ck;
}

static void deg_to_nmea(double deg, bool is_lat, char *out, size_t out_len)
{
    double abs_deg = (deg < 0.0) ? -deg : deg;
    int d = (int)abs_deg;
    double m = (abs_deg - (double)d) * 60.0;
    if (is_lat) {
        snprintf(out, out_len, "%02d%010.7f", d, m); /* DDMM.mmmmmmm */
    } else {
        snprintf(out, out_len, "%03d%010.7f", d, m); /* DDDMM.mmmmmmm */
    }
}

/**
 * NMEA GGA-style fix quality (0–5) from UBX-NAV-PVT fixType + flags.
 * Aligns with common GGA definitions: 0 invalid, 1 GPS, 2 DGPS, 3 PPS, 4 RTK fixed, 5 RTK float.
 */
static int nav_pvt_to_nmea_quality(uint8_t fix_type, uint8_t flags)
{
    /* UBX-NAV-PVT flags:
     * bit0: gnssFixOK
     * bit1: diffSoln
     * bits6..7: carrSoln (0 none, 1 float, 2 fixed)
     */
    const bool gnss_fix_ok = (flags & 0x01u) != 0u;
    const bool diff_soln = (flags & 0x02u) != 0u;
    const uint8_t carr_soln = (flags >> 6) & 0x03u;

    if (fix_type == 0u || fix_type == 1u || !gnss_fix_ok) {
        return 0;
    }
    /* u-blox: 5 = time only fix — report as PPS for app contract */
    if (fix_type == 5u) {
        return 3;
    }
    if (carr_soln == 2u) {
        return 4;
    }
    if (carr_soln == 1u) {
        return 5;
    }
    if (diff_soln) {
        return 2;
    }
    /* 2D / 3D / GNSS+DR without differential carrier solution */
    if (fix_type >= 2u) {
        return 1;
    }
    return 0;
}

static void build_gga_from_nav_pvt(const uint8_t *pvt, uint8_t num_sv, char *out, size_t out_len)
{
    const uint8_t hour = pvt[8];
    const uint8_t min = pvt[9];
    const uint8_t sec = pvt[10];
    const int32_t nano = rd_i32_le(&pvt[16]);
    const uint8_t fix_type = pvt[20];
    const uint8_t flags = pvt[21];
    const int32_t lon = rd_i32_le(&pvt[24]);    /* 1e-7 deg */
    const int32_t lat = rd_i32_le(&pvt[28]);    /* 1e-7 deg */
    const int32_t hmsl_mm = rd_i32_le(&pvt[36]); /* mm */
    const uint16_t pDOP = rd_u16_le(&pvt[76]);  /* 0.01 */

    const char ns = (lat >= 0) ? 'N' : 'S';
    const char ew = (lon >= 0) ? 'E' : 'W';
    const int quality = nav_pvt_to_nmea_quality(fix_type, flags);
    const double lat_deg = (double)lat / 1e7;
    const double lon_deg = (double)lon / 1e7;
    const double alt_m = (double)hmsl_mm / 1000.0;
    const double hdop = (double)pDOP / 100.0; /* fallback: NAV-PVT pDOP */

    int centi = nano / 10000000; /* hundredths */
    if (centi < 0) {
        centi = 0;
    } else if (centi > 99) {
        centi = 99;
    }

    char time_str[16];
    char lat_str[20];
    char lon_str[20];
    snprintf(time_str, sizeof(time_str), "%02u%02u%02u.%02d", (unsigned)hour, (unsigned)min,
             (unsigned)sec, centi);
    deg_to_nmea(lat_deg, true, lat_str, sizeof(lat_str));
    deg_to_nmea(lon_deg, false, lon_str, sizeof(lon_str));

    char body[128];
    snprintf(body, sizeof(body), "GNGGA,%s,%s,%c,%s,%c,%d,%02u,%.1f,%.2f,M,,M,,", time_str,
             lat_str, ns, lon_str, ew, quality, (unsigned)num_sv, hdop, alt_m);

    const uint8_t ck = nmea_checksum_body(body);
    snprintf(out, out_len, "$%s*%02X\r\n", body, (unsigned)ck);
}

typedef struct {
    bool saw_valid_ubx;
    uint8_t last_navsat_numsv;
    uint8_t last_pvt_numsv;
    uint32_t last_pvt_itow_ms;
} rtk_parse_state_t;

typedef struct {
    bool have_time;
    bool have_date;
    bool have_speed_course;
    bool have_pdop;
    bool have_gsa_hdop;
    bool have_gsa_vdop;
    bool have_sat_visible;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    float speed_m_s;
    float heading_deg;
    float pdop;
    float gsa_hdop;
    float gsa_vdop;
    uint8_t sat_visible;
} nmea_state_t;

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool nmea_parse_hhmmss(const char *s, uint8_t *hh, uint8_t *mm, uint8_t *ss)
{
    if (!s || strlen(s) < 6 || !isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) ||
        !isdigit((unsigned char)s[2]) || !isdigit((unsigned char)s[3]) ||
        !isdigit((unsigned char)s[4]) || !isdigit((unsigned char)s[5])) {
        return false;
    }
    *hh = (uint8_t)((s[0] - '0') * 10 + (s[1] - '0'));
    *mm = (uint8_t)((s[2] - '0') * 10 + (s[3] - '0'));
    *ss = (uint8_t)((s[4] - '0') * 10 + (s[5] - '0'));
    return true;
}

static bool nmea_parse_ddmmyy(const char *s, uint16_t *year, uint8_t *month, uint8_t *day)
{
    if (!s || strlen(s) < 6 || !isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) ||
        !isdigit((unsigned char)s[2]) || !isdigit((unsigned char)s[3]) ||
        !isdigit((unsigned char)s[4]) || !isdigit((unsigned char)s[5])) {
        return false;
    }
    *day = (uint8_t)((s[0] - '0') * 10 + (s[1] - '0'));
    *month = (uint8_t)((s[2] - '0') * 10 + (s[3] - '0'));
    uint8_t yy = (uint8_t)((s[4] - '0') * 10 + (s[5] - '0'));
    *year = (yy >= 80u) ? (uint16_t)(1900u + yy) : (uint16_t)(2000u + yy);
    return true;
}

static bool nmea_parse_latlon(const char *val, const char *hemi, bool is_lat, float *deg_out)
{
    if (!val || !hemi || !*val || !*hemi) {
        return false;
    }
    char *endp = NULL;
    double raw = strtod(val, &endp);
    if (endp == val) {
        return false;
    }
    (void)is_lat;
    int deg = (int)(raw / 100.0);
    double min = raw - ((double)deg * 100.0);
    double dec_deg = (double)deg + (min / 60.0);
    char h = (char)toupper((unsigned char)hemi[0]);
    if (h == 'S' || h == 'W') {
        dec_deg = -dec_deg;
    }
    *deg_out = (float)dec_deg;
    return true;
}

static void nmea_publish_fix(const char *gga_sentence, uint8_t fix_quality, uint8_t num_sv,
                             float hdop, float alt_m, float lat_deg, float lon_deg,
                             rtk_parse_state_t *ps, nmea_state_t *ns)
{
    uint8_t fix_type = 0;
    if (fix_quality == 0u) {
        fix_type = 0u;
    } else if (fix_quality >= 4u) {
        fix_type = 3u;
    } else if (fix_quality >= 1u) {
        fix_type = 2u;
    }

    telemetry_nav_status_t nav = {
        .valid = true,
        .source = 3u, /* NMEA-derived */
        .diff_soln = (fix_quality >= 2u),
        .gps_fix = fix_type,
        .itow_ms = ps->last_pvt_itow_ms + 1000u,
    };
    ps->last_pvt_itow_ms = nav.itow_ms;
    telemetry_set_nav_status(&nav);

    telemetry_rtk_t r = {0};
    r.valid = true;
    r.fix_type = fix_type;
    r.fix_quality_code = fix_quality;
    r.num_sv = num_sv;
    r.num_sv_visible = ns->have_sat_visible ? ns->sat_visible : num_sv;
    r.hour = ns->have_time ? ns->hour : 0u;
    r.min = ns->have_time ? ns->min : 0u;
    r.sec = ns->have_time ? ns->sec : 0u;
    r.year = ns->have_date ? ns->year : 0u;
    r.month = ns->have_date ? ns->month : 0u;
    r.day = ns->have_date ? ns->day : 0u;
    r.lat_deg = lat_deg;
    r.lon_deg = lon_deg;
    r.h_msl_m = alt_m;
    r.h_acc_m = 0.0f;
    r.v_acc_m = 0.0f;
    r.g_speed_m_s = ns->have_speed_course ? ns->speed_m_s : 0.0f;
    r.heading_deg = ns->have_speed_course ? ns->heading_deg : 0.0f;
    r.vel_n = 0.0f;
    r.vel_e = 0.0f;
    r.vel_d = 0.0f;
    r.pdop = ns->have_pdop ? ns->pdop : hdop;
    (void)strncpy(r.gga, gga_sentence, sizeof(r.gga) - 1);
    telemetry_set_rtk(&r);
}

static void nmea_process_sentence(char *line, rtk_parse_state_t *ps, nmea_state_t *ns)
{
    if (!line || line[0] != '$') {
        return;
    }
    char *star = strchr(line, '*');
    if (!star || (star - line) < 2 || strlen(star) < 3) {
        return;
    }
    uint8_t ck = 0;
    for (char *p = line + 1; p < star; p++) {
        ck ^= (uint8_t)(*p);
    }
    int hi = hex_nibble(star[1]);
    int lo = hex_nibble(star[2]);
    if (hi < 0 || lo < 0) {
        return;
    }
    uint8_t got_ck = (uint8_t)((hi << 4) | lo);
    if (got_ck != ck) {
        return;
    }
    char validated_sentence[160];
    snprintf(validated_sentence, sizeof(validated_sentence), "%s\r\n", line);
    *star = '\0';

    char *fields[24] = {0};
    int n_fields = 0;
    char *save = NULL;
    for (char *tok = strtok_r(line + 1, ",", &save); tok && n_fields < 24;
         tok = strtok_r(NULL, ",", &save)) {
        fields[n_fields++] = tok;
    }
    if (n_fields <= 0 || !fields[0] || strlen(fields[0]) < 5) {
        return;
    }
    const char *type = fields[0] + 2; /* GPxxx/GNxxx -> xxx */

    if (strcmp(type, "RMC") == 0) {
        if (n_fields > 1) {
            uint8_t hh, mm, ss;
            if (nmea_parse_hhmmss(fields[1], &hh, &mm, &ss)) {
                ns->hour = hh;
                ns->min = mm;
                ns->sec = ss;
                ns->have_time = true;
            }
        }
        if (n_fields > 7 && fields[7] && *fields[7]) {
            ns->speed_m_s = (float)(strtod(fields[7], NULL) * 0.514444);
            ns->have_speed_course = true;
        }
        if (n_fields > 8 && fields[8] && *fields[8]) {
            ns->heading_deg = (float)strtod(fields[8], NULL);
            ns->have_speed_course = true;
        }
        if (n_fields > 9) {
            uint16_t year;
            uint8_t month, day;
            if (nmea_parse_ddmmyy(fields[9], &year, &month, &day)) {
                ns->year = year;
                ns->month = month;
                ns->day = day;
                ns->have_date = true;
            }
        }
    } else if (strcmp(type, "GSA") == 0) {
        /* $--GSA: f[15]=PDOP, f[16]=HDOP, f[17]=VDOP (after talker+msg token). */
        if (n_fields > 15 && fields[15] && *fields[15]) {
            ns->pdop = (float)strtod(fields[15], NULL);
            ns->have_pdop = true;
        }
        if (n_fields > 16 && fields[16] && *fields[16]) {
            ns->gsa_hdop = (float)strtod(fields[16], NULL);
            ns->have_gsa_hdop = true;
        }
        if (n_fields > 17 && fields[17] && *fields[17]) {
            ns->gsa_vdop = (float)strtod(fields[17], NULL);
            ns->have_gsa_vdop = true;
        }
    } else if (strcmp(type, "GSV") == 0) {
        if (n_fields > 3 && fields[3] && *fields[3]) {
            long vis = strtol(fields[3], NULL, 10);
            if (vis >= 0 && vis <= 255) {
                ns->sat_visible = (uint8_t)vis;
                ns->have_sat_visible = true;
                telemetry_set_rtk_sat_visible(ns->sat_visible);
            }
        }
    } else if (strcmp(type, "GGA") == 0) {
        if (n_fields < 10) {
            return;
        }
        uint8_t hh, mm, ss;
        if (nmea_parse_hhmmss(fields[1], &hh, &mm, &ss)) {
            ns->hour = hh;
            ns->min = mm;
            ns->sec = ss;
            ns->have_time = true;
        }
        float lat_deg = 0.0f, lon_deg = 0.0f;
        if (!nmea_parse_latlon(fields[2], fields[3], true, &lat_deg) ||
            !nmea_parse_latlon(fields[4], fields[5], false, &lon_deg)) {
            return;
        }
        long q = strtol(fields[6], NULL, 10);
        long sv = strtol(fields[7], NULL, 10);
        float hdop = (fields[8] && *fields[8]) ? (float)strtod(fields[8], NULL) : 0.0f;
        float alt_m = (fields[9] && *fields[9]) ? (float)strtod(fields[9], NULL) : 0.0f;
        if (q < 0 || q > 255 || sv < 0 || sv > 255) {
            return;
        }
        ps->saw_valid_ubx = true; /* suppress "no UBX" startup note when NMEA is flowing */
        nmea_publish_fix(validated_sentence, (uint8_t)q, (uint8_t)sv, hdop, alt_m, lat_deg, lon_deg,
                         ps, ns);
    }
}

typedef enum {
    UBX_SYNC1,
    UBX_SYNC2,
    UBX_CLASS,
    UBX_ID,
    UBX_LEN1,
    UBX_LEN2,
    UBX_PAYLOAD,
    UBX_CK_A,
    UBX_CK_B,
} ubx_state_t;

void rtk_task(void *pvParameters)
{
    (void)pvParameters;

    uart_config_t cfg = {
        .baud_rate = CFG_RTK_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Larger RX for RTCM bursts when corrections are injected on the same UART. */
    if (uart_driver_install(CFG_RTK_UART_NUM, 8192, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "UART install failed");
    } else if (uart_param_config(CFG_RTK_UART_NUM, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "UART config failed");
    } else if (uart_set_pin(CFG_RTK_UART_NUM, CFG_RTK_UART_TX_GPIO, CFG_RTK_UART_RX_GPIO,
                            UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "UART set_pin failed");
    } else {
        ESP_LOGI(TAG, "RTK UART on TX=%d RX=%d baud=%d", CFG_RTK_UART_TX_GPIO, CFG_RTK_UART_RX_GPIO,
                 CFG_RTK_UART_BAUD);
    }

    /* Keep module defaults on startup: NMEA stays enabled (primary output mode). */
    (void)uart_flush_input(CFG_RTK_UART_NUM);

    ubx_state_t st = UBX_SYNC1;
    uint8_t cls = 0, id = 0;
    uint16_t len = 0;
    uint16_t payload_read = 0;
    uint8_t ck_a = 0, ck_b = 0;
    uint8_t rx_ck_a = 0, rx_ck_b = 0;
    uint32_t packets_bad = 0;

    rtk_parse_state_t ps = {0};
    uint32_t startup_deadline_ms = 2000;
    bool startup_ubx_notice_emitted = false;
    uint32_t waited_ms = 0;

    /* Store first bytes of payload for summaries we care about. */
    uint8_t payload_head[96];
    nmea_state_t nmea = {0};
    char nmea_line[160];
    size_t nmea_line_len = 0;
    bool nmea_collect = false;

    uint8_t buf[256];
    while (1) {
        int n = uart_read_bytes(CFG_RTK_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(200));
        waited_ms += 200;
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                uint8_t b = buf[i];
                if (b == '$') {
                    nmea_collect = true;
                    nmea_line_len = 0;
                    nmea_line[nmea_line_len++] = (char)b;
                } else if (nmea_collect) {
                    if (b == '\n' || b == '\r') {
                        nmea_line[nmea_line_len] = '\0';
                        nmea_process_sentence(nmea_line, &ps, &nmea);
                        nmea_collect = false;
                        nmea_line_len = 0;
                    } else if (nmea_line_len < (sizeof(nmea_line) - 1)) {
                        nmea_line[nmea_line_len++] = (char)b;
                    } else {
                        nmea_collect = false;
                        nmea_line_len = 0;
                    }
                }

                switch (st) {
                case UBX_SYNC1:
                    if (b == 0xB5) {
                        st = UBX_SYNC2;
                    }
                    break;
                case UBX_SYNC2:
                    if (b == 0x62) {
                        ck_a = 0;
                        ck_b = 0;
                        cls = 0;
                        id = 0;
                        len = 0;
                        payload_read = 0;
                        memset(payload_head, 0, sizeof(payload_head));
                        st = UBX_CLASS;
                    } else if (b == 0xB5) {
                        /* stay in sync hunt */
                        st = UBX_SYNC2;
                    } else {
                        st = UBX_SYNC1;
                    }
                    break;
                case UBX_CLASS:
                    cls = b;
                    ubx_ck_update(b, &ck_a, &ck_b);
                    st = UBX_ID;
                    break;
                case UBX_ID:
                    id = b;
                    ubx_ck_update(b, &ck_a, &ck_b);
                    st = UBX_LEN1;
                    break;
                case UBX_LEN1:
                    len = (uint16_t)b;
                    ubx_ck_update(b, &ck_a, &ck_b);
                    st = UBX_LEN2;
                    break;
                case UBX_LEN2:
                    len |= (uint16_t)b << 8;
                    ubx_ck_update(b, &ck_a, &ck_b);
                    payload_read = 0;
                    st = (len == 0) ? UBX_CK_A : UBX_PAYLOAD;
                    break;
                case UBX_PAYLOAD:
                    if (payload_read < sizeof(payload_head)) {
                        payload_head[payload_read] = b;
                    }
                    ubx_ck_update(b, &ck_a, &ck_b);
                    payload_read++;
                    if (payload_read >= len) {
                        st = UBX_CK_A;
                    }
                    break;
                case UBX_CK_A:
                    rx_ck_a = b;
                    st = UBX_CK_B;
                    break;
                case UBX_CK_B:
                    rx_ck_b = b;
                    if (rx_ck_a == ck_a && rx_ck_b == ck_b) {
                        ps.saw_valid_ubx = true;

                        /* Minimal "parser v1": satellite count.
                         * For u-blox style UBX:
                         * - NAV-SAT (01 35): payload[5] = numSvs (len >= 8)
                         * - NAV-PVT (01 07): payload[23] = numSV  (len >= 24)
                         */
                        if (cls == 0x01 && id == 0x35 && len >= 8 && sizeof(payload_head) >= 6) {
                            uint8_t numsv = payload_head[5];
                            ps.last_navsat_numsv = numsv;
                            telemetry_set_rtk_sat_visible(numsv);
                        } else if (cls == 0x01 && id == 0x03 && len >= 6 && sizeof(payload_head) >= 6) {
                            /* UBX-NAV-STATUS: iTOW @0, gpsFix @4, flags @5 (bit0 diffSoln). */
                            uint32_t itow = rd_u32_le(&payload_head[0]);
                            uint8_t gps_fix = payload_head[4];
                            uint8_t flags = payload_head[5];
                            telemetry_nav_status_t ns = {
                                .valid = true,
                                .source = 1u,
                                .diff_soln = (flags & 0x01u) != 0u,
                                .gps_fix = gps_fix,
                                .itow_ms = itow,
                            };
                            telemetry_set_nav_status(&ns);
                        } else if (cls == 0x01 && id == 0x07 && len >= 24 && sizeof(payload_head) >= 24) {
                            uint8_t numsv = payload_head[23];
                            if (numsv != ps.last_pvt_numsv) {
                                ps.last_pvt_numsv = numsv;
                            }
                            /* "Basic data" (NAV-PVT, u-blox style, len 92). */
                            if (len >= 92 && sizeof(payload_head) >= 92) {
                                uint32_t iTOW = rd_u32_le(&payload_head[0]);
                                uint8_t fixType = payload_head[20];
                                uint8_t pvt_flags = payload_head[21];
                                /* Many receivers never send NAV-STATUS; mirror diffSoln from NAV-PVT. */
                                telemetry_nav_status_t ns_pvt = {
                                    .valid = true,
                                    .source = 2u,
                                    .diff_soln = (pvt_flags & 0x02u) != 0u,
                                    .gps_fix = fixType,
                                    .itow_ms = iTOW,
                                };
                                telemetry_set_nav_status(&ns_pvt);

                                uint16_t year = rd_u16_le(&payload_head[4]);
                                uint8_t month = payload_head[6];
                                uint8_t day = payload_head[7];
                                uint8_t hour = payload_head[8];
                                uint8_t min = payload_head[9];
                                uint8_t sec = payload_head[10];
                                int32_t lon = rd_i32_le(&payload_head[24]); /* 1e-7 deg */
                                int32_t lat = rd_i32_le(&payload_head[28]); /* 1e-7 deg */
                                int32_t hmsl_mm = rd_i32_le(&payload_head[36]);
                                uint32_t hAcc_mm = rd_u32_le(&payload_head[40]);
                                uint32_t vAcc_mm = rd_u32_le(&payload_head[44]);
                                int32_t velN = rd_i32_le(&payload_head[48]); /* mm/s */
                                int32_t velE = rd_i32_le(&payload_head[52]); /* mm/s */
                                int32_t velD = rd_i32_le(&payload_head[56]); /* mm/s */
                                int32_t gSpeed = rd_i32_le(&payload_head[60]); /* mm/s */
                                int32_t headMot = rd_i32_le(&payload_head[64]); /* 1e-5 deg */
                                uint16_t pDOP = rd_u16_le(&payload_head[76]); /* 0.01 */

                                if (iTOW != ps.last_pvt_itow_ms) {
                                    ps.last_pvt_itow_ms = iTOW;
                                    uint8_t sat_vis =
                                        ps.last_navsat_numsv ? ps.last_navsat_numsv : ps.last_pvt_numsv;
                                    telemetry_rtk_t r = {
                                        .valid = true,
                                        .fix_type = fixType,
                                        .fix_quality_code =
                                            (uint8_t)nav_pvt_to_nmea_quality(fixType, pvt_flags),
                                        .num_sv = numsv,
                                        .num_sv_visible = sat_vis,
                                        .year = year,
                                        .month = month,
                                        .day = day,
                                        .hour = hour,
                                        .min = min,
                                        .sec = sec,
                                        .lat_deg = (float)((double)lat / 1e7),
                                        .lon_deg = (float)((double)lon / 1e7),
                                        .h_msl_m = (float)((double)hmsl_mm / 1000.0),
                                        .h_acc_m = (float)((double)hAcc_mm / 1000.0),
                                        .v_acc_m = (float)((double)vAcc_mm / 1000.0),
                                        .g_speed_m_s = (float)((double)gSpeed / 1000.0),
                                        .heading_deg = (float)((double)headMot / 1e5),
                                        .vel_n = (float)((double)velN / 1000.0),
                                        .vel_e = (float)((double)velE / 1000.0),
                                        .vel_d = (float)((double)velD / 1000.0),
                                        .pdop = (float)((double)pDOP / 100.0),
                                    };
                                    char gga[160];
                                    build_gga_from_nav_pvt(payload_head, numsv, gga, sizeof(gga));
                                    (void)strncpy(r.gga, gga, sizeof(r.gga) - 1);
                                    telemetry_set_rtk(&r);
                                    // ESP_LOGI(TAG, "GGA: %s", gga);
                                }
                            }
                        }
                    } else {
                        packets_bad++;
                        if ((packets_bad % 100u) == 1u) {
                            ESP_LOGW(TAG,
                                     "UBX checksum errors (count=%" PRIu32 ")",
                                     packets_bad);
                        }
                    }
                    st = UBX_SYNC1;
                    break;
                default:
                    st = UBX_SYNC1;
                    break;
                }
            }
        }

        /* After startup config: ensure we actually see UBX. */
        if (!ps.saw_valid_ubx && !startup_ubx_notice_emitted && waited_ms >= startup_deadline_ms) {
            ESP_LOGI(TAG,
                     "No valid UBX seen within initial %u ms; running with NMEA-primary startup",
                     (unsigned)startup_deadline_ms);
            /* Emit this startup note once; UBX may still arrive later. */
            startup_ubx_notice_emitted = true;
        }
    }
}
