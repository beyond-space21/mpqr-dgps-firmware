#include "network/ws_server.h"

#include <stdio.h>
#include <string.h>
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
    if (!rtk->valid || rtk->fix_type < 3) {
        return "NO_FIX";
    }
    return (rtk->h_acc_m <= 0.05f && rtk->v_acc_m <= 0.10f) ? "FIX" : "FLOAT";
}

static void reset_clients(void)
{
    for (size_t i = 0; i < sizeof(s_clients) / sizeof(s_clients[0]); i++) {
        s_clients[i] = -1;
    }
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
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (frame.len == 0) {
        return ESP_OK;
    }
    char *buf = malloc(frame.len + 1);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    frame.payload = (uint8_t *)buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    free(buf);
    return err;
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

        char json[320];
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
