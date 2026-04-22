#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "freertos/FreeRTOS.h"
#include "screens/dashboard_screen.h"

typedef struct {
    dashboard_screen_t *dashboard;
    uint8_t battery_percentage;
    TickType_t next_render_tick;
} app_controller_t;

void app_controller_init(app_controller_t *app, dashboard_screen_t *dashboard);
void app_controller_update(app_controller_t *app);

#endif
