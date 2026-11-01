/**
 * @file config.h
 * @brief Compatibility shim: upstream Metalio `config.h` -> CrossMetal pinout.
 *
 * The vendored EPDiy board glue reads pin and timing values through the same
 * macro names the vendor firmware uses. This header maps those names onto
 * `metalio_pinout.h`.
 *
 * Names that are already spelled identically in `metalio_pinout.h` — the EPD bus
 * pins, the TPS control bits and the expander I/O numbers — are NOT redefined
 * here. They arrive through the include, and a self-referential
 * `#define EPD_PIN_D0 EPD_PIN_D0` would only produce a redefinition warning.
 * Only genuinely renamed values appear below.
 */

#ifndef CROSSMETAL_EPD_CONFIG_SHIM_H
#define CROSSMETAL_EPD_CONFIG_SHIM_H

#include "metalio_pinout.h"

/* --- panel geometry ---------------------------------------------------------- */

#define DISPLAY_WIDTH METALIO_DISPLAY_WIDTH
#define DISPLAY_HEIGHT METALIO_DISPLAY_HEIGHT
#define DISPLAY_WHITE_BORDER_PIXELS METALIO_WHITE_BORDER_PIXELS
#define DISPLAY_CONTENT_LEFT_INSET METALIO_CONTENT_LEFT_INSET
#define DISPLAY_CONTENT_RIGHT_INSET METALIO_CONTENT_RIGHT_INSET
#define DISPLAY_CONTENT_TOP_INSET METALIO_CONTENT_TOP_INSET
#define DISPLAY_CONTENT_BOTTOM_INSET METALIO_CONTENT_BOTTOM_INSET
#define DISPLAY_TOUCH_OFFSET_X METALIO_TOUCH_OFFSET_X
#define DISPLAY_TOUCH_OFFSET_Y METALIO_TOUCH_OFFSET_Y

#define EPD_VCOM_MV METALIO_EPD_VCOM_MV

/* --- I2C --------------------------------------------------------------------- */

#define I2C_SDA_PIN METALIO_I2C_SDA_PIN
#define I2C_SCL_PIN METALIO_I2C_SCL_PIN
#define IO_EXPANDER_INT_GPIO METALIO_IO_EXPANDER_INT_GPIO
#define ANA_SW_GPIO METALIO_ANA_SW_GPIO

/* --- frontlight -------------------------------------------------------------- */

#define FL_COOL_PIN METALIO_FL_COOL_PIN
#define FL_WARM_PIN METALIO_FL_WARM_PIN

/* --- power key timing -------------------------------------------------------- */

#define POWER_LONG_PRESS_MS METALIO_POWER_LONG_PRESS_MS
#define PWR_PULSE_INTERVAL_MS METALIO_PWR_PULSE_INTERVAL_MS
#define PWR_PULSE_COUNT METALIO_PWR_PULSE_COUNT

/* --- misc peripherals -------------------------------------------------------- */

#define VIBRATION_MOTOR_PULSE_MS METALIO_VIBRATION_MOTOR_PULSE_MS
#define TOUCH_I2C_ADDR METALIO_TOUCH_I2C_ADDR
#define TOUCH_INT_PIN METALIO_TOUCH_INT_PIN
#define PCF8563_I2C_ADDR METALIO_PCF8563_I2C_ADDR
#define RTC_INT_GPIO METALIO_RTC_INT_GPIO

/*
 * The EPD parallel-bus pins (EPD_PIN_*), the TPS65185 control bits
 * (EPD_TPS_IO_*) and the expander I/O numbers (EPD_IO_*) keep the exact names
 * upstream uses and come straight from metalio_pinout.h.
 */

#endif /* CROSSMETAL_EPD_CONFIG_SHIM_H */