#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "esp_err.h"

esp_err_t display_driver_init(void);
void display_driver_show(void);
void display_driver_process(void);

#endif
