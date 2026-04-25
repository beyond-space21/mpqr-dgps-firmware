#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app/app_bootstrap.h"

static const char *TAG = "main";

void app_main(void)
{
    /* WiFi default INFO is very chatty; raises scheduling noise next to GNSS UART work. */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);

    ESP_LOGI(TAG, "DGPS init");
    app_bootstrap_start();

    ESP_LOGI(TAG, "All tasks started");
    vTaskDelete(NULL);
}
