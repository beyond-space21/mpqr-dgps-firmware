#include "display/display_touch_cst820.h"

#include "config.h"
#include "i2c_bus.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst820.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "touch_cst820";

/* Match display_driver.c portrait layout (DISP_HOR_RES × DISP_VER_RES). */
#define TOUCH_LVGL_HOR 410
#define TOUCH_LVGL_VER 502
#define TOUCH_RELEASE_HOLD_MS 35

static esp_lcd_panel_io_handle_t s_touch_io;
static esp_lcd_touch_handle_t s_touch;
static lv_indev_drv_t s_indev_drv;
static lv_indev_t *s_touch_indev;
static SemaphoreHandle_t s_touch_irq_sem;
static uint16_t s_last_x;
static uint16_t s_last_y;
static bool s_last_pressed;
static TickType_t s_last_press_tick;

static void IRAM_ATTR touch_irq_isr(void *arg)
{
    (void)arg;
    BaseType_t hpw = pdFALSE;
    if (s_touch_irq_sem) {
        xSemaphoreGiveFromISR(s_touch_irq_sem, &hpw);
    }
    if (hpw == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void lv_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;
    const TickType_t now = xTaskGetTickCount();
    bool has_fresh_irq = false;

    if (s_touch_irq_sem && xSemaphoreTake(s_touch_irq_sem, 0) == pdTRUE) {
        has_fresh_irq = true;
    }

    if (has_fresh_irq) {
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(40)) != pdTRUE) {
            data->state = s_last_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
            data->point.x = s_last_x;
            data->point.y = s_last_y;
            return;
        }

        (void)esp_lcd_touch_read_data(tp);

        uint16_t x[1];
        uint16_t y[1];
        uint16_t strength[1];
        uint8_t cnt = 0;
        const bool pressed = esp_lcd_touch_get_coordinates(tp, x, y, strength, &cnt, 1);
        xSemaphoreGive(i2c_mutex);

        if (pressed && cnt > 0) {
            s_last_x = x[0];
            s_last_y = y[0];
            s_last_pressed = true;
            s_last_press_tick = now;
        } else {
            s_last_pressed = false;
        }
    }

    if (s_last_pressed && (now - s_last_press_tick) <= pdMS_TO_TICKS(TOUCH_RELEASE_HOLD_MS)) {
        data->point.x = s_last_x;
        data->point.y = s_last_y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        s_last_pressed = false;
        data->state = LV_INDEV_STATE_REL;
    }
}

esp_err_t display_touch_cst820_init(void)
{
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not ready");

    s_touch_irq_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_touch_irq_sem, ESP_ERR_NO_MEM, TAG, "touch irq sem alloc failed");

    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        vSemaphoreDelete(s_touch_irq_sem);
        s_touch_irq_sem = NULL;
        return ESP_ERR_TIMEOUT;
    }

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    io_cfg.dev_addr = CFG_TOUCH_I2C_ADDR;
    io_cfg.scl_speed_hz = 100000;

    esp_err_t err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &s_touch_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_i2c failed: %s", esp_err_to_name(err));
        goto fail;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = TOUCH_LVGL_HOR - 1,
        .y_max = TOUCH_LVGL_VER - 1,
        .rst_gpio_num = (CFG_TOUCH_RESET_GPIO >= 0) ? (gpio_num_t)CFG_TOUCH_RESET_GPIO : GPIO_NUM_NC,
        .int_gpio_num = (CFG_TOUCH_INT_GPIO >= 0) ? (gpio_num_t)CFG_TOUCH_INT_GPIO : GPIO_NUM_NC,
        .levels =
            {
                .reset = 0,
                .interrupt = 0,
            },
        .flags =
            {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        /* NULL: avoids second gpio_install_isr_service (see app_main); we poll INT for data-ready. */
        .interrupt_callback = NULL,
    };

    err = esp_lcd_touch_new_i2c_cst820(s_touch_io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_cst820 failed: %s", esp_err_to_name(err));
        goto fail_io;
    }

    if (CFG_TOUCH_INT_GPIO >= 0) {
        ESP_ERROR_CHECK(gpio_set_pull_mode((gpio_num_t)CFG_TOUCH_INT_GPIO, GPIO_PULLUP_ONLY));
        ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)CFG_TOUCH_INT_GPIO, touch_irq_isr, NULL));
    }

    xSemaphoreGive(i2c_mutex);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = lv_touch_read_cb;
    s_indev_drv.user_data = s_touch;
    s_touch_indev = lv_indev_drv_register(&s_indev_drv);
    ESP_RETURN_ON_FALSE(s_touch_indev, ESP_ERR_NO_MEM, TAG, "lv_indev register failed");

    ESP_LOGI(TAG, "CST820 ready (I2C 0x%02x RST=%d INT=%d, LVGL %d×%d)", CFG_TOUCH_I2C_ADDR, CFG_TOUCH_RESET_GPIO,
             CFG_TOUCH_INT_GPIO, TOUCH_LVGL_HOR, TOUCH_LVGL_VER);
    return ESP_OK;

fail_io:
    if (s_touch_io) {
        esp_lcd_panel_io_del(s_touch_io);
        s_touch_io = NULL;
    }
fail:
    xSemaphoreGive(i2c_mutex);
    if (s_touch_irq_sem) {
        vSemaphoreDelete(s_touch_irq_sem);
        s_touch_irq_sem = NULL;
    }
    return err;
}
