# CrossMetal — parallel EPDiy display for the ED047TC2 panel.
#
# VENDORED from https://github.com/CloudZao/Metalio-E-INK4-Plus (MIT),
# paths under `main/boards/metalio-e-ink4-plus/display/`.
#
# The vendored sources are kept as close to the vendor tree as possible so a
# future `diff` against upstream stays meaningful. Adaptation happens only at the
# seams, through the shim headers in `include/` (config.h, metalio_i2c1.h,
# pca9555.h, tps65185.h, metalio_sd.h), which map upstream names onto the
# CrossMetal board support component.
#
# lut.S is deliberately not built: it is Xtensa/S3 vector-extension assembly
# guarded by `#ifdef CONFIG_IDF_TARGET_ESP32S3`, so it is empty on RISC-V.
# epdiy_riscv_ve_stub.c supplies the equivalent C implementations for S31.

set(EPD_SRC
    "src/epdiy.c"
    "src/epd_board.c"
    "src/epd_board_common.c"
    "src/board_specific.c"
    "src/displays.c"
    "src/render.c"
    "src/highlevel.c"
    "src/builtin_waveforms.c"
    "src/epdiy_riscv_ve_stub.c"
    "src/epd_board_metalio_eink4_plus.c"

    # CrossMetal facade over the vendored stack.
    "src/cm_display.c"

    "src/output_common/line_queue.c"
    "src/output_common/lut.c"
    "src/output_common/render_context.c"
    "src/output_common/render_method.c"

    "src/output_lcd/lcd_driver.c"
    "src/output_lcd/render_lcd.c"

    "src/waveforms/my_waveform.c"
    "src/waveforms/my_waveform_trim.c"
)

idf_component_register(
    SRCS ${EPD_SRC}
    INCLUDE_DIRS
        "include"
        "src"
    PRIV_INCLUDE_DIRS
        "src/output_common"
        "src/output_lcd"
        "src/waveforms"
    REQUIRES
        metalio_bsp
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_rmt
        esp_lcd
        esp_adc
        esp_mm
        esp_hal_i2s
        esp_timer
        esp_psram
        freertos
)

# Make `pca9555_set_value` route through the board's shared expander image, so the
# vendored glue and the BSP cannot hold divergent shadows of the same register.
# See the comment in include/pca9555.h.
target_compile_definitions(${COMPONENT_LIB} PRIVATE EPDIO_PCA9555_BOARD_OWNED=1)

# The parallel bus driver reaches into IDF internals for the LCD/CAM peripheral
# and GDMA. These paths are not part of any public component; upstream's own
# build does the same thing.
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${IDF_PATH}/components/esp_lcd/include"
    "${IDF_PATH}/components/hal/include"
    "${IDF_PATH}/components/esp_hw_support/include"
)

# IDF 6 renamed or moved several HAL headers the vendored driver includes; the
# compat header re-provides them.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-missing-field-initializers
    -Wno-sign-compare
)