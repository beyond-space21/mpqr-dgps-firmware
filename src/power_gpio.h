#ifndef POWER_GPIO_H
#define POWER_GPIO_H

#include "esp_err.h"

/**
 * Configure SYSOFF (output, held LOW), CHG (input, no pull — external defines level),
 * and BUTTON (input, pull-up).
 * Call early in app_main so SYSOFF is latched before other subsystems draw current.
 */
esp_err_t power_gpio_init(void);

/** Sample CHG pin: true if charger is connected (pin reads LOW). */
bool power_gpio_charger_connected(void);

/** Sample button: true if pressed (reads LOW). */
bool power_gpio_button_pressed(void);

#endif
