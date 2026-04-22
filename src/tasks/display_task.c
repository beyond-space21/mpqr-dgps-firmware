#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display_task.h"
#include "app/app_controller.h"
#include "display/display_driver.h"
#include "screens/dashboard_screen.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "display_task";

#define LVGL_TASK_PERIOD_MS 10

void display_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Display task init");
    ESP_ERROR_CHECK(display_driver_init());

    dashboard_screen_t *dashboard = dashboard_screen_create();
    ESP_ERROR_CHECK(dashboard ? ESP_OK : ESP_ERR_NO_MEM);

    app_controller_t app;
    app_controller_init(&app, dashboard);
    display_driver_show();

    while (1) {
        app_controller_update(&app);
        display_driver_process();
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}
