#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ntrip_task.h"
#include "esp_log.h"

static const char *TAG = "ntrip_task";

void ntrip_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "NTRIP placeholder — add websocket client and UART forwarder");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
