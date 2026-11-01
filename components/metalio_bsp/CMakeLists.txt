# CrossMetal — Metalio E-Ink4-Plus board support component.
#
# Ported from https://github.com/CloudZao/Metalio-E-INK4-Plus (MIT).
# See NOTICE.md in the repository root for attribution.

idf_component_register(
    SRCS
        "src/cm_i2c.c"
        "src/cm_tca9555.c"
        "src/cm_tps65185.c"
        "src/cm_board.c"
        "src/cm_power.c"
        "src/cm_keys.c"
        "src/cm_frontlight.c"
        "src/cm_storage.c"
        "src/cm_rtc.c"
        "src/cm_touch_ft6336u.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_driver_i2c
        esp_driver_gpio
        esp_driver_sdmmc
        esp_driver_ledc
        esp_timer
        esp_psram
        fatfs
        sdmmc
        freertos
)