#ifndef OPERATION_LOG_WIDGET_H
#define OPERATION_LOG_WIDGET_H

#include <stdbool.h>
#include "lvgl.h"

typedef struct operation_log_widget operation_log_widget_t;

operation_log_widget_t *operation_log_widget_create(lv_obj_t *parent);
void operation_log_widget_set_visible(operation_log_widget_t *widget, bool visible);
void operation_log_widget_render(operation_log_widget_t *widget, const char *text);

#endif
