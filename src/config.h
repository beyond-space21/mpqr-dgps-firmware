/**
 * Central pin and timing configuration for DGPS firmware.
 * BNO055 values mirror Kconfig (sdkconfig); adjust others for your PCB.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "sdkconfig.h"

/* I2C bus (shared: BNO055, fuel gauge, touch) */
#define CFG_I2C_PORT                I2C_NUM_0
#define CFG_BNO055_SDA_GPIO         CONFIG_BNO055_SDA_PIN
#define CFG_BNO055_SCL_GPIO         CONFIG_BNO055_SCL_PIN
#define CFG_BNO055_I2C_ADDR         CONFIG_BNO055_I2C_ADDR
#define CFG_BNO055_I2C_FREQ_KHZ       CONFIG_BNO055_I2C_FREQUENCY

/* BQ27441-G1A fuel gauge (same I2C bus as BNO055; 7-bit addr 0x55) */
#define CFG_BQ27441_I2C_ADDR        0x55
/** CST820 / CST8xx 7-bit I2C (scan showed 0x15). */
#define CFG_TOUCH_I2C_ADDR          0x15
/** Touch panel reset (active low), TP_RST. */
#define CFG_TOUCH_RESET_GPIO        16
/** Touch interrupt input, TP_INT. */
#define CFG_TOUCH_INT_GPIO          5

/* RTK GNSS module UART */
#define CFG_RTK_UART_NUM            UART_NUM_1
#define CFG_RTK_UART_TX_GPIO        7
#define CFG_RTK_UART_RX_GPIO        6
#define CFG_RTK_UART_BAUD           115200

/* NTRIP corrections → forwarded to RTK on same UART as configured in rtk_task */
#define CFG_WIFI_STA_SSID            "bluvern"
#define CFG_WIFI_STA_PASSWORD        "Texas@3467"
#define CFG_WIFI_RECONNECT_MS        5000

#define CFG_NTRIP_HOST               "103.206.29.4"
#define CFG_NTRIP_PORT               2105
#define CFG_NTRIP_MOUNTPOINT         "IVRS"
#define CFG_NTRIP_USERNAME           "kvm12012000"
#define CFG_NTRIP_PASSWORD           "cors@2022"
#define CFG_NTRIP_TLS                0
#define CFG_NTRIP_RECONNECT_MS       5000
/** Send last known NMEA GGA to caster on this interval (for VRS / network-RTK). 0 = disabled. */
#define CFG_NTRIP_SEND_GGA_MS        10000

/* Colour OLED over QSPI — placeholder pins (use SPI2_HOST when wiring display) */
#define CFG_DISPLAY_CS_GPIO         10
#define CFG_DISPLAY_DC_GPIO         11

/* Power / charger (BQ24079): button input (LOW = pressed), SYSOFF out (LOW = run), CHG in (LOW = plugged) */
#define CFG_BUTTON_GPIO             4
#define CFG_SYSOFF_GPIO             47
#define CFG_CHG_GPIO                1

#endif
