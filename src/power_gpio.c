#include "power_gpio.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "power_gpio";

esp_err_t power_gpio_init(void)
{
    /* SYSOFF: keep system powered — start LOW and never float. */
    gpio_config_t sysoff = {
        .pin_bit_mask = 1ULL << CFG_SYSOFF_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&sysoff);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SYSOFF gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }
    gpio_set_level(CFG_SYSOFF_GPIO, 0);

    /* CHG: input, no pull — BQ24079 drives open-drain style status. */
    gpio_config_t chg = {
        .pin_bit_mask = 1ULL << CFG_CHG_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&chg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CHG gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Button: input, pull-up; edges handled by button_service after ISR service install. */
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << CFG_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    err = gpio_config(&btn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BUTTON gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Power GPIO ready (SYSOFF=%d LOW, CHG=%d, BTN=%d pull-up)", CFG_SYSOFF_GPIO,
             CFG_CHG_GPIO, CFG_BUTTON_GPIO);
    return ESP_OK;
}

bool power_gpio_charger_connected(void)
{
    return gpio_get_level(CFG_CHG_GPIO) == 0;
}

bool power_gpio_button_pressed(void)
{
    return gpio_get_level(CFG_BUTTON_GPIO) == 0;
}
