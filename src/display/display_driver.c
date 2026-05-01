#include "display/display_driver.h"
#include "display/display_touch_cst820.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "display_driver";

// #define LANDSCAPE

static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},

    #ifdef LANDSCAPE
        {0x36, (uint8_t[]){0xA0}, 1, 0},
    #endif
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0x60}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x11, NULL, 0, 60},
    {0x29, NULL, 0, 20},
};

#ifdef LANDSCAPE
#define LCD_WIDTH 410
#define LCD_HEIGHT 502
#define LCD_COL_OFFSET 22
#define LCD_ROW_OFFSET 10

#else
#define LCD_WIDTH 502
#define LCD_HEIGHT 410
#define LCD_COL_OFFSET 0
#define LCD_ROW_OFFSET 22
#endif


#define LCD_CS 10
#define LCD_SCLK 12
#define LCD_SDIO0 11
#define LCD_SDIO1 13
#define LCD_SDIO2 14
#define LCD_SDIO3 15
#define TFT_RST 17
#define VCI_EN 18

#define DISP_HOR_RES LCD_HEIGHT
#define DISP_VER_RES LCD_WIDTH
#define LCD_QSPI_PCLK_HZ (30 * 1000 * 1000)
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_BUF_LINES 40
#define LCD_DMA_BOUNCE_LINES 12

#define LCD_HOST SPI2_HOST

static esp_lcd_panel_handle_t s_panel_handle;
static lv_disp_draw_buf_t s_lvgl_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_color_t *s_lvgl_buf1;
static lv_color_t *s_lvgl_buf2;
static lv_color_t *s_dma_bounce_buf;
static uint16_t *s_clear_line_buf;
static SemaphoreHandle_t s_lcd_flush_done_sem;

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    BaseType_t hp_task_woken = pdFALSE;
    if (sem != NULL) {
        xSemaphoreGiveFromISR(sem, &hp_task_woken);
    }
    if (hp_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
    return false;
}

static void clear_panel_solid(uint16_t color565)
{
    if (!s_clear_line_buf) {
        s_clear_line_buf = heap_caps_malloc(DISP_HOR_RES * sizeof(uint16_t), MALLOC_CAP_DMA);
        ESP_ERROR_CHECK(s_clear_line_buf ? ESP_OK : ESP_ERR_NO_MEM);
    }
    for (int x = 0; x < DISP_HOR_RES; x++) {
        s_clear_line_buf[x] = color565;
    }
    for (int y = 0; y < DISP_VER_RES; y++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel_handle, 0, y, DISP_HOR_RES, y + 1, s_clear_line_buf));
    }
}

static void lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_map)
{
    const int area_w = area->x2 - area->x1 + 1;
    const int area_h = area->y2 - area->y1 + 1;
    const size_t bytes_per_pixel = sizeof(lv_color_t);

    if (s_lcd_flush_done_sem != NULL) {
        while (xSemaphoreTake(s_lcd_flush_done_sem, 0) == pdTRUE) {
        }
    }

    int row = 0;
    while (row < area_h) {
        int chunk_rows = area_h - row;
        if (chunk_rows > LCD_DMA_BOUNCE_LINES) {
            chunk_rows = LCD_DMA_BOUNCE_LINES;
        }
        const size_t chunk_pixels = (size_t)area_w * (size_t)chunk_rows;
        const lv_color_t *chunk_src = color_map + ((size_t)row * (size_t)area_w);
        memcpy(s_dma_bounce_buf, chunk_src, chunk_pixels * bytes_per_pixel);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel_handle, area->x1, area->y1 + row,
                                                  area->x2 + 1, area->y1 + row + chunk_rows,
                                                  s_dma_bounce_buf));
        if (s_lcd_flush_done_sem != NULL) {
            (void)xSemaphoreTake(s_lcd_flush_done_sem, pdMS_TO_TICKS(100));
        }
        row += chunk_rows;
    }
    lv_disp_flush_ready(disp_drv);
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

esp_err_t display_driver_init(void)
{
    ESP_LOGI(TAG, "Init CO5300 display");

    gpio_set_direction(VCI_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(VCI_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(300));

    spi_bus_config_t buscfg =
        CO5300_PANEL_BUS_QSPI_CONFIG(LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3,
                                     LCD_WIDTH * 120 * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    io_config.pclk_hz = LCD_QSPI_PCLK_HZ;
    io_config.trans_queue_depth = 10;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    co5300_vendor_config_t vendor_config = {
        .flags.use_qspi_interface = 1,
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(co5300_lcd_init_cmd_t),
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io_handle, &panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel_handle, LCD_ROW_OFFSET, LCD_COL_OFFSET));
    clear_panel_solid(0x0000);

    lv_init();

    const size_t lvgl_buf_pixels = DISP_HOR_RES * LVGL_BUF_LINES;
    s_lvgl_buf1 = heap_caps_malloc(lvgl_buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lvgl_buf1 == NULL) {
        s_lvgl_buf1 = heap_caps_malloc(lvgl_buf_pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    s_lvgl_buf2 = heap_caps_malloc(lvgl_buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lvgl_buf2 == NULL) {
        s_lvgl_buf2 = heap_caps_malloc(lvgl_buf_pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    s_dma_bounce_buf = heap_caps_malloc(DISP_HOR_RES * LCD_DMA_BOUNCE_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    s_lcd_flush_done_sem = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(s_lvgl_buf1 ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(s_lvgl_buf2 ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(s_dma_bounce_buf ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(s_lcd_flush_done_sem ? ESP_OK : ESP_ERR_NO_MEM);
    lv_disp_draw_buf_init(&s_lvgl_draw_buf, s_lvgl_buf1, s_lvgl_buf2, lvgl_buf_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = DISP_HOR_RES;
    s_disp_drv.ver_res = DISP_VER_RES;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_lvgl_draw_buf;
    s_disp_drv.full_refresh = 0;

    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = notify_lvgl_flush_ready};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, s_lcd_flush_done_sem));
    lv_disp_drv_register(&s_disp_drv);

    ESP_ERROR_CHECK(display_touch_cst820_init());

    const esp_timer_create_args_t lvgl_tick_timer_args = {.callback = lvgl_tick_cb, .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    return ESP_OK;
}

void display_driver_show(void)
{
    lv_timer_handler();
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));
}

void display_driver_process(void)
{
    lv_timer_handler();
}
