/**
 * Central pin and timing configuration for DGPS firmware.
 * BNO055 values mirror Kconfig (sdkconfig); adjust others for your PCB.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "sdkconfig.h"

/* -------------------------------------------------------------------------- */
/* Power / charger / user button (ESP32-S3)                                   */
/* -------------------------------------------------------------------------- */
/** User button: pressed = LOW, internal pull-up enabled in init. */
#define CFG_BUTTON_GPIO             4
/**
 * Hold power latch: drive LOW to keep the system powered ON.
 * Drive HIGH to release shutdown (PMIC / latch turns rail off).
 */
#define CFG_SYSOFF_GPIO             47
/**
 * BQ24079 CHG status: LOW = charger connected, HIGH = no charger.
 * Note: GPIO1 is a strapping pin on ESP32-S3; ensure your schematic keeps
 * it valid at reset for your flash voltage / boot mode requirements.
 */
#define CFG_CHG_GPIO                1

/** Debounce after GPIO interrupt before sampling stable level (ms). */
#define CFG_BUTTON_DEBOUNCE_MS      50
/** Only a full hold of this duration counts as a valid press (ms). */
#define CFG_BUTTON_LONG_PRESS_MS    2000

/** Display refresh while charging (low duty). */
#define CFG_DISPLAY_CHARGING_PERIOD_MS   5000
/** Display refresh while waiting for boot long-press confirmation. */
#define CFG_DISPLAY_BOOT_AWAIT_PERIOD_MS 200
/** Normal operation display cadence (existing behavior baseline). */
#define CFG_DISPLAY_OPERATION_PERIOD_MS  500

/* I2C bus (shared: BNO055, fuel gauge, touch) */
#define CFG_I2C_PORT                I2C_NUM_0
#define CFG_BNO055_SDA_GPIO         CONFIG_BNO055_SDA_PIN
#define CFG_BNO055_SCL_GPIO         CONFIG_BNO055_SCL_PIN
#define CFG_BNO055_I2C_ADDR         CONFIG_BNO055_I2C_ADDR
#define CFG_BNO055_I2C_FREQ_KHZ       CONFIG_BNO055_I2C_FREQUENCY

/* BQ27441-G1A fuel gauge (same I2C bus as BNO055; 7-bit addr 0x55) */
#define CFG_BQ27441_I2C_ADDR        0x55
#define CFG_TOUCH_I2C_ADDR          0x38

/* RTK GNSS module UART */
#define CFG_RTK_UART_NUM            UART_NUM_1
#define CFG_RTK_UART_TX_GPIO        7
#define CFG_RTK_UART_RX_GPIO        6
#define CFG_RTK_UART_BAUD           115200

/* NTRIP corrections → forwarded to RTK on same UART as configured in rtk_task */

/* Colour OLED over QSPI — placeholder pins (use SPI2_HOST when wiring display) */
#define CFG_DISPLAY_CS_GPIO         10
#define CFG_DISPLAY_DC_GPIO         11

#endif
