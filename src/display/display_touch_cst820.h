#ifndef DISPLAY_TOUCH_CST820_H
#define DISPLAY_TOUCH_CST820_H

#include "esp_err.h"

/**
 * CST820 on shared I2C (esp_lcd_touch_cst820). Call after lv_init() and lv_disp_drv_register().
 * Uses CFG_TOUCH_* from config.h; coordinates match CO5300 portrait (410×502).
 */
esp_err_t display_touch_cst820_init(void);

#endif
