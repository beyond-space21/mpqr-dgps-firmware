#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app/app_bootstrap.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "DGPS init");
    app_bootstrap_start();

    ESP_LOGI(TAG, "All tasks started");
    vTaskDelete(NULL);
}
