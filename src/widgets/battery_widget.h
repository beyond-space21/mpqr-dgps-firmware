#ifndef BATTERY_WIDGET_H
#define BATTERY_WIDGET_H

#include <stdbool.h>
#include "lvgl.h"

typedef struct battery_widget battery_widget_t;

battery_widget_t *battery_widget_create(lv_obj_t *parent);
void battery_widget_set_visible(battery_widget_t *widget, bool visible);
void battery_widget_render(battery_widget_t *widget, uint8_t battery_percentage);

#endif
