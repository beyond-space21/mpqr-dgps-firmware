#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtk_task.h"
#include "config.h"
#include "telemetry.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
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

static int gga_quality_from_nav_pvt(uint8_t fix_type, uint8_t flags)
{
    /* UBX-NAV-PVT flags:
     * bit1: diffSoln
     * bits6..7: carrSoln (0 none, 1 float, 2 fixed)
     */
    const bool gnss_fix_ok = (flags & 0x01u) != 0u;
    const bool diff_soln = (flags & 0x02u) != 0u;
    const uint8_t carr_soln = (flags >> 6) & 0x03u;

    if (fix_type < 3 || !gnss_fix_ok) {
        return 0;
    }
    if (!diff_soln) {
        return 1; /* GPS/SPS */
    }
    if (carr_soln == 2u) {
        return 4; /* RTK fixed */
    }
    if (carr_soln == 1u) {
        return 5; /* RTK float */
    }
    return 2; /* DGPS */
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
    const int quality = gga_quality_from_nav_pvt(fix_type, flags);
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

static esp_err_t po_wait_ok_fail(uart_port_t uart, int timeout_ms)
{
    /* BK166X spec §5.1: command reply is "$OK" or "$FAIL". */
    char line[32];
    size_t line_len = 0;
    uint8_t b = 0;

    int waited_ms = 0;
    while (waited_ms < timeout_ms) {
        int n = uart_read_bytes(uart, &b, 1, pdMS_TO_TICKS(20));
        waited_ms += 20;
        if (n <= 0) {
            continue;
        }

        if (b == '\r') {
            continue;
        }
        if (b == '\n') {
            line[line_len] = '\0';
            if (strcmp(line, "$OK") == 0) {
                return ESP_OK;
            }
            if (strcmp(line, "$FAIL") == 0) {
                return ESP_FAIL;
            }
            line_len = 0;
            continue;
        }

        if (line_len + 1 < sizeof(line)) {
            line[line_len++] = (char)b;
        } else {
            line_len = 0;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t rtk_disable_nmea_output_on_startup(uart_port_t uart)
{
    /* BK166X spec §5.7: "$POLCFGMSG,1,0" => disable all NMEA messages. */
    static const char cmd[] = "$POLCFGMSG,1,0\r\n";
    uart_write_bytes(uart, cmd, sizeof(cmd) - 1);
    esp_err_t err = po_wait_ok_fail(uart, 500);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BK166X: NMEA disabled ($OK)");
    } else {
        ESP_LOGW(TAG, "BK166X: NMEA disable did not return $OK: %s", esp_err_to_name(err));
    }
    return err;
}

typedef struct {
    bool saw_valid_ubx;
    uint8_t last_navsat_numsv;
    uint8_t last_pvt_numsv;
    uint32_t last_pvt_itow_ms;
} rtk_parse_state_t;

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

    if (uart_driver_install(CFG_RTK_UART_NUM, 2048, 0, 0, NULL, 0) != ESP_OK) {
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

    /* Disable NMEA on every startup (BK166X PO_NMEA command). */
    (void)uart_flush_input(CFG_RTK_UART_NUM);
    (void)rtk_disable_nmea_output_on_startup(CFG_RTK_UART_NUM);

    ubx_state_t st = UBX_SYNC1;
    uint8_t cls = 0, id = 0;
    uint16_t len = 0;
    uint16_t payload_read = 0;
    uint8_t ck_a = 0, ck_b = 0;
    uint8_t rx_ck_a = 0, rx_ck_b = 0;
    uint32_t packets_bad = 0;

    rtk_parse_state_t ps = {0};
    uint32_t startup_deadline_ms = 2000;
    uint32_t waited_ms = 0;

    /* Store first bytes of payload for summaries we care about. */
    uint8_t payload_head[96];

    uint8_t buf[256];
    while (1) {
        int n = uart_read_bytes(CFG_RTK_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(200));
        waited_ms += 200;
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                uint8_t b = buf[i];

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
                        } else if (cls == 0x01 && id == 0x07 && len >= 24 && sizeof(payload_head) >= 24) {
                            uint8_t numsv = payload_head[23];
                            if (numsv != ps.last_pvt_numsv) {
                                ps.last_pvt_numsv = numsv;
                            }
                            /* "Basic data" (NAV-PVT, u-blox style, len 92). */
                            if (len >= 92 && sizeof(payload_head) >= 92) {
                                uint32_t iTOW = rd_u32_le(&payload_head[0]);
                                uint16_t year = rd_u16_le(&payload_head[4]);
                                uint8_t month = payload_head[6];
                                uint8_t day = payload_head[7];
                                uint8_t hour = payload_head[8];
                                uint8_t min = payload_head[9];
                                uint8_t sec = payload_head[10];
                                uint8_t fixType = payload_head[20];
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
                                    telemetry_set_rtk(&r);

                                    char gga[160];
                                    build_gga_from_nav_pvt(payload_head, numsv, gga, sizeof(gga));
                                    ESP_LOGI(TAG, "GGA: %s", gga);
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
        if (!ps.saw_valid_ubx && waited_ms >= startup_deadline_ms) {
            ESP_LOGW(TAG, "No valid UBX seen within %u ms after NMEA-disable ($OK seen? check wiring/baud/output)",
                     (unsigned)startup_deadline_ms);
            /* Avoid repeating forever. */
            ps.saw_valid_ubx = true;
        }
    }
}
