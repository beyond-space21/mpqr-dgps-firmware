#ifndef DASHBOARD_SCREEN_H
#define DASHBOARD_SCREEN_H

#include <stdint.h>
#include "telemetry.h"

typedef struct dashboard_screen dashboard_screen_t;

dashboard_screen_t *dashboard_screen_create(void);
void dashboard_screen_render_boot_wait(dashboard_screen_t *screen);
void dashboard_screen_render_onboarding(dashboard_screen_t *screen, const char *title, const char *detail);
void dashboard_screen_render_charging(dashboard_screen_t *screen, uint8_t battery_percentage);
void dashboard_screen_render_operation(dashboard_screen_t *screen, const telemetry_t *telemetry);

#endif
