#include "network/ws_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "config.h"
#include "driver/uart.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "telemetry.h"

static const char *TAG = "ws_server";
static httpd_handle_t s_httpd;
static TaskHandle_t s_tx_task;
static int s_clients[4];

#if !CONFIG_HTTPD_WS_SUPPORT
esp_err_t ws_server_start(void)
{
    ESP_LOGW(TAG, "WebSocket support disabled (CONFIG_HTTPD_WS_SUPPORT=0)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ws_server_stop(void)
{
    return ESP_OK;
}

bool ws_server_is_running(void)
{
    return false;
}
#else

static const char *rtk_fix_status(const telemetry_rtk_t *rtk)
{
    if (!rtk->valid) {
        return "No fix";
    }
    switch (rtk->fix_quality_code) {
        case 0:
            return "No fix";
        case 1:
            return "GPS fix";
        case 2:
            return "DGPS fix";
        case 3:
            return "PPS fix";
        case 4:
            return "RTK fix";
        case 5:
            return "RTK float";
        default:
            return "No fix";
    }
}

static void reset_clients(void)
{
    for (size_t i = 0; i < sizeof(s_clients) / sizeof(s_clients[0]); i++) {
        s_clients[i] = -1;
    }
}

/** Max WS payload forwarded to RTK UART (corrections / RTCM / binary chunks). */
#define WS_TO_RTK_MAX_PAYLOAD 16384
/** Refuse to buffer larger frames (avoids OOM); connection may reset — keep app chunks smaller. */
#define WS_RECV_HARD_MAX 65536

static esp_err_t rtk_uart_write_all(const uint8_t *data, size_t len, size_t *written_out)
{
    if (len == 0u || data == NULL) {
        if (written_out) {
            *written_out = 0u;
        }
        return ESP_OK;
    }
    if (!uart_is_driver_installed(CFG_RTK_UART_NUM)) {
        if (written_out) {
            *written_out = 0u;
        }
        return ESP_ERR_INVALID_STATE;
    }
    size_t off = 0;
    while (off < len) {
        const int n = uart_write_bytes(CFG_RTK_UART_NUM, (const char *)(data + off), (int)(len - off));
        if (n < 0) {
            if (written_out) {
                *written_out = off;
            }
            return ESP_FAIL;
        }
        if (n == 0) {
            if (written_out) {
                *written_out = off;
            }
            return ESP_ERR_TIMEOUT;
        }
        off += (size_t)n;
    }
    if (written_out) {
        *written_out = off;
    }
    return ESP_OK;
}

/** RTCM3 frames start with 0xD3; WS continuation chunks may start mid-stream. */
static const char *ws_rtcm_payload_hint(httpd_ws_type_t typ, const uint8_t *p, size_t len)
{
    if (len == 0u || p == NULL) {
        return "empty";
    }
    if (typ == HTTPD_WS_TYPE_CONTINUE) {
        return "frag";
    }
    return (p[0] == 0xD3u) ? "D3" : "noD3";
}

/** UART gets opcode 0x2 (binary) and 0x0 (continuation of a binary message) only — never text. */
static bool ws_frame_is_binary_payload(httpd_ws_type_t t)
{
    return (t == HTTPD_WS_TYPE_BINARY) || (t == HTTPD_WS_TYPE_CONTINUE);
}

static void register_client(int fd)
{
    for (size_t i = 0; i < sizeof(s_clients) / sizeof(s_clients[0]); i++) {
        if (s_clients[i] == fd) {
            return;
        }
    }
    for (size_t i = 0; i < sizeof(s_clients) / sizeof(s_clients[0]); i++) {
        if (s_clients[i] < 0) {
            s_clients[i] = fd;
            ESP_LOGI(TAG, "WebSocket client connected (fd=%d)", fd);
            return;
        }
    }
    ESP_LOGW(TAG, "WebSocket client rejected (no free slot, fd=%d)", fd);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        register_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }

    if (frame.len == 0u) {
        if (frame.type == HTTPD_WS_TYPE_PING) {
            httpd_ws_frame_t pong = {.final = true,
                                     .fragmented = false,
                                     .type = HTTPD_WS_TYPE_PONG,
                                     .payload = NULL,
                                     .len = 0};
            return httpd_ws_send_frame(req, &pong);
        }
        return ESP_OK;
    }

    if (frame.len > WS_RECV_HARD_MAX) {
        ESP_LOGE(TAG, "WS frame len %u > hard max %u", (unsigned)frame.len, (unsigned)WS_RECV_HARD_MAX);
        return ESP_FAIL;
    }

    uint8_t *buf = (uint8_t *)malloc(frame.len + 1u);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    buf[frame.len] = '\0';
    frame.payload = buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    if (ws_frame_is_binary_payload(frame.type)) {
        telemetry_t t;
        telemetry_get_copy(&t);
        if (t.nav_status.valid) {
            const char *src = (t.nav_status.source == 1u)   ? "UBX-STATUS"
                              : (t.nav_status.source == 2u) ? "UBX-PVT"
                                                            : "?";
            const char *rtc = ws_rtcm_payload_hint(frame.type, frame.payload, frame.len);
            const unsigned fb = (frame.len > 0u) ? (unsigned)frame.payload[0] : 0u;
            if (t.rtk.valid) {
                ESP_LOGI(TAG,
                         "received %u bytes → RTK UART | diffSoln=%u src=%s fix=%u iTOW=%" PRIu32
                         " ms qual=%u hAcc=%.2fm | fb=0x%02X rtc=%s",
                         (unsigned)frame.len, (unsigned)(t.nav_status.diff_soln ? 1u : 0u), src,
                         (unsigned)t.nav_status.gps_fix, (uint32_t)t.nav_status.itow_ms,
                         (unsigned)t.rtk.fix_quality_code, (double)t.rtk.h_acc_m, fb, rtc);
            } else {
                ESP_LOGI(TAG,
                         "received %u bytes → RTK UART | diffSoln=%u src=%s fix=%u iTOW=%" PRIu32
                         " ms | fb=0x%02X rtc=%s",
                         (unsigned)frame.len, (unsigned)(t.nav_status.diff_soln ? 1u : 0u), src,
                         (unsigned)t.nav_status.gps_fix, (uint32_t)t.nav_status.itow_ms, fb, rtc);
            }
        } else {
            const char *rtc = ws_rtcm_payload_hint(frame.type, frame.payload, frame.len);
            const unsigned fb = (frame.len > 0u) ? (unsigned)frame.payload[0] : 0u;
            ESP_LOGI(TAG,
                     "received %u bytes → RTK UART | no UBX nav snapshot (rtk_task RX or PVT missing?) | "
                     "fb=0x%02X rtc=%s",
                     (unsigned)frame.len, fb, rtc);
        }
        if (frame.len <= WS_TO_RTK_MAX_PAYLOAD) {
            size_t written = 0;
            err = rtk_uart_write_all(frame.payload, frame.len, &written);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "RTK UART forward: %s (wrote %u/%u)", esp_err_to_name(err),
                         (unsigned)written, (unsigned)frame.len);
            } else {
                (void)written;
            }
        } else {
            ESP_LOGW(TAG, "WS frame len %u exceeds forward cap %u; discarded", (unsigned)frame.len,
                     (unsigned)WS_TO_RTK_MAX_PAYLOAD);
        }
    } else if (frame.type == HTTPD_WS_TYPE_PING) {
        httpd_ws_frame_t pong = {.final = true,
                                 .fragmented = false,
                                 .type = HTTPD_WS_TYPE_PONG,
                                 .payload = frame.payload,
                                 .len = frame.len};
        err = httpd_ws_send_frame(req, &pong);
        free(buf);
        return err;
    }

    free(buf);
    return ESP_OK;
}

static void ws_tx_task(void *arg)
{
    (void)arg;
    while (ws_server_is_running()) {
        telemetry_t t;
        telemetry_get_copy(&t);
        char gga_clean[160];
        (void)strncpy(gga_clean, t.rtk.gga, sizeof(gga_clean) - 1);
        gga_clean[sizeof(gga_clean) - 1] = '\0';
        for (char *p = gga_clean; *p != '\0'; ++p) {
            if (*p == '\r' || *p == '\n') {
                *p = '\0';
                break;
            }
        }

        char json[512];
        (void)snprintf(json, sizeof(json),
                       "{\"horizontal_accuracy\":%.3f,\"vertical_accuracy\":%.3f,"
                       "\"RTK_fix_status\":\"%s\",\"lat\":%.7f,\"lng\":%.7f,"
                       "\"satellites\":%u,\"GGA\":\"%s\"}",
                       (double)t.rtk.h_acc_m, (double)t.rtk.v_acc_m, rtk_fix_status(&t.rtk),
                       (double)t.rtk.lat_deg, (double)t.rtk.lon_deg,
                       (unsigned)(t.rtk.num_sv_visible ? t.rtk.num_sv_visible : t.rtk.num_sv),
                       gga_clean);

        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json,
            .len = strlen(json),
        };
        for (size_t i = 0; i < sizeof(s_clients) / sizeof(s_clients[0]); i++) {
            if (s_clients[i] < 0) {
                continue;
            }
            esp_err_t err = httpd_ws_send_frame_async(s_httpd, s_clients[i], &frame);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "WebSocket send failed, dropping client fd=%d err=%s",
                         s_clients[i], esp_err_to_name(err));
                s_clients[i] = -1;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    s_tx_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ws_server_start(void)
{
    if (s_httpd != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    err = httpd_register_uri_handler(s_httpd, &ws_uri);
    if (err != ESP_OK) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        return err;
    }

    reset_clients();
    if (s_tx_task == NULL) {
        xTaskCreate(ws_tx_task, "ws_tx", 4096, NULL, 4, &s_tx_task);
    }
    ESP_LOGI(TAG, "WebSocket server ready at /ws");
    return ESP_OK;
}

esp_err_t ws_server_stop(void)
{
    if (s_httpd != NULL) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    reset_clients();
    return ESP_OK;
}

bool ws_server_is_running(void)
{
    return s_httpd != NULL;
}
#endif
