# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
    REQUIRES bno055 esp_driver_i2c esp_driver_uart esp_driver_gpio lvgl esp_lcd esp_lcd_co5300 esp_lcd_touch_cst820
)
