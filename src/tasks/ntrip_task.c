#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ntrip_task.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "config.h"
#include "esp_log.h"
#include "lwip/netdb.h"
#include "network/rtcm_forward.h"
#include "network/wifi_manager.h"
#include "telemetry.h"

static const char *TAG = "ntrip_task";

/** Caps select() sleep so recv stays responsive between GGA uploads. */
#define NTRIP_SELECT_CAP_MS          250

static void set_ntrip_status(telemetry_ntrip_state_t state, const char *last_error)
{
    telemetry_ntrip_t n = {
        .state = state,
    };
    if (last_error != NULL) {
        (void)strncpy(n.last_error, last_error, sizeof(n.last_error) - 1);
    }
    telemetry_set_ntrip(&n);
}

static void base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_len)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0;
    size_t j = 0;
    while (i < in_len && (j + 4) < out_len) {
        size_t rem = in_len - i;
        uint32_t octet_a = in[i++];
        uint32_t octet_b = (rem > 1u) ? in[i++] : 0u;
        uint32_t octet_c = (rem > 2u) ? in[i++] : 0u;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        out[j++] = table[(triple >> 18) & 0x3F];
        out[j++] = table[(triple >> 12) & 0x3F];
        out[j++] = (rem > 1u) ? table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (rem > 2u) ? table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
}

static int open_ntrip_socket(void)
{
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_buf[8];
    (void)snprintf(port_buf, sizeof(port_buf), "%d", CFG_NTRIP_PORT);

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(CFG_NTRIP_HOST, port_buf, &hints, &res);
    if (rc != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS failed for %s:%s (%d)", CFG_NTRIP_HOST, port_buf, rc);
        return -1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return -1;
    }
    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        close(sock);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    return sock;
}

static esp_err_t ntrip_login(int sock)
{
    char userpass[192];
    (void)snprintf(userpass, sizeof(userpass), "%s:%s", CFG_NTRIP_USERNAME, CFG_NTRIP_PASSWORD);
    char auth_b64[320];
    base64_encode((const uint8_t *)userpass, strlen(userpass), auth_b64, sizeof(auth_b64));

    char req[640];
    (void)snprintf(req, sizeof(req),
                   "GET /%s HTTP/1.0\r\n"
                   "User-Agent: NTRIP ESP32DGPS/1.0\r\n"
                   "Accept: */*\r\n"
                   "Connection: close\r\n"
                   "Authorization: Basic %s\r\n\r\n",
                   CFG_NTRIP_MOUNTPOINT, auth_b64);

    ssize_t sent = send(sock, req, strlen(req), 0);
    if (sent < 0) {
        return ESP_FAIL;
    }

    char resp[256];
    ssize_t n = recv(sock, resp, sizeof(resp) - 1, 0);
    if (n <= 0) {
        return ESP_FAIL;
    }
    resp[n] = '\0';

    if (strstr(resp, "401") != NULL || strstr(resp, "403") != NULL) {
        set_ntrip_status(TELEMETRY_NTRIP_AUTH_FAILED, "auth_failed");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (strstr(resp, "200") == NULL && strstr(resp, "ICY 200") == NULL) {
        set_ntrip_status(TELEMETRY_NTRIP_ERROR, "bad_http_status");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static bool ntrip_try_send_gga(int sock)
{
#if CFG_NTRIP_SEND_GGA_MS <= 0
    (void)sock;
    return false;
#else
    telemetry_t tel = {0};
    telemetry_get_copy(&tel);
    if (!tel.rtk.valid || tel.rtk.gga[0] == '\0') {
        return false;
    }
    size_t len = strnlen(tel.rtk.gga, sizeof(tel.rtk.gga));
    ssize_t ws = send(sock, tel.rtk.gga, len, 0);
    if (ws < 0) {
        ESP_LOGD(TAG, "NTRIP GGA send failed errno=%d", errno);
        return false;
    }
    return (size_t)ws == len;
#endif
}

void ntrip_task(void *pvParameters)
{
    (void)pvParameters;

    if (CFG_NTRIP_TLS) {
        ESP_LOGW(TAG, "CFG_NTRIP_TLS=1 is not implemented in this build; using TCP only");
    }
    set_ntrip_status(TELEMETRY_NTRIP_DISCONNECTED, NULL);

    while (1) {
        if (!wifi_manager_is_connected()) {
            set_ntrip_status(TELEMETRY_NTRIP_DISCONNECTED, "wifi_not_connected");
            vTaskDelay(pdMS_TO_TICKS(CFG_NTRIP_RECONNECT_MS));
            continue;
        }

        set_ntrip_status(TELEMETRY_NTRIP_CONNECTING, NULL);
        int sock = open_ntrip_socket();
        if (sock < 0) {
            ESP_LOGW(TAG, "NTRIP socket connect failed (errno=%d)", errno);
            set_ntrip_status(TELEMETRY_NTRIP_ERROR, "socket_connect_failed");
            vTaskDelay(pdMS_TO_TICKS(CFG_NTRIP_RECONNECT_MS));
            continue;
        }

        esp_err_t auth = ntrip_login(sock);
        if (auth != ESP_OK) {
            ESP_LOGW(TAG, "NTRIP login failed");
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(CFG_NTRIP_RECONNECT_MS));
            continue;
        }

        ESP_LOGI(TAG, "ntrip: connected (streaming corrections)");
        set_ntrip_status(TELEMETRY_NTRIP_STREAMING, NULL);

#if CFG_NTRIP_SEND_GGA_MS > 0
        TickType_t last_gga_tick = xTaskGetTickCount() - pdMS_TO_TICKS(CFG_NTRIP_SEND_GGA_MS);
#endif

        uint8_t rx[512];
        while (wifi_manager_is_connected()) {
            int timeout_ms = NTRIP_SELECT_CAP_MS;

#if CFG_NTRIP_SEND_GGA_MS > 0
            {
                const TickType_t now = xTaskGetTickCount();
                const TickType_t period = pdMS_TO_TICKS(CFG_NTRIP_SEND_GGA_MS);
                TickType_t elapsed = now - last_gga_tick;
                if (elapsed >= period) {
                    timeout_ms = 0;
                } else {
                    uint32_t rem_ms = (uint32_t)((period - elapsed) * portTICK_PERIOD_MS);
                    if (rem_ms < (uint32_t)timeout_ms) {
                        timeout_ms = (int)rem_ms;
                        if (timeout_ms <= 0) {
                            timeout_ms = 1;
                        }
                    }
                }
            }
#endif

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            struct timeval tv = {
                .tv_sec = timeout_ms / 1000,
                .tv_usec = (timeout_ms % 1000) * 1000,
            };
            int sr = select(sock + 1, &rfds, NULL, NULL, &tv);

            if (sr < 0) {
                if (errno != EINTR) {
                    ESP_LOGW(TAG, "select failed errno=%d", errno);
                    break;
                }
                continue;
            }

            if (sr > 0 && FD_ISSET(sock, &rfds)) {
                ssize_t n = recv(sock, rx, sizeof(rx), 0);
                if (n <= 0) {
                    break;
                }
                ESP_LOGI(TAG, "ntrip: byte (%u)", (unsigned)n);
                size_t written = 0;
                esp_err_t fwd = rtcm_forward_to_rtk_uart(rx, (size_t)n, &written);
                if (fwd != ESP_OK) {
                    ESP_LOGW(TAG, "RTCM forward failed (%s, %u/%u)",
                             esp_err_to_name(fwd), (unsigned)written, (unsigned)n);
                }
            }

#if CFG_NTRIP_SEND_GGA_MS > 0
            if ((xTaskGetTickCount() - last_gga_tick) >= pdMS_TO_TICKS(CFG_NTRIP_SEND_GGA_MS)) {
                if (ntrip_try_send_gga(sock)) {
                    last_gga_tick = xTaskGetTickCount();
                } else {
                    TickType_t period = pdMS_TO_TICKS(CFG_NTRIP_SEND_GGA_MS);
                    TickType_t cap = pdMS_TO_TICKS(NTRIP_SELECT_CAP_MS);
                    TickType_t backoff = (cap < period) ? (period - cap) : (period ? period / 2 : 1);
                    last_gga_tick = xTaskGetTickCount() - backoff;
                }
            }
#endif
        }

        close(sock);
        set_ntrip_status(TELEMETRY_NTRIP_DISCONNECTED, "stream_closed");
        vTaskDelay(pdMS_TO_TICKS(CFG_NTRIP_RECONNECT_MS));
    }
}
