/**
 * @file metalio_pinout.h
 * @brief CrossMetal — Metalio E-Ink4-Plus hardware pinout.
 *
 * TRANSCRIBED VERBATIM from the upstream hardware repository:
 *   https://github.com/CloudZao/Metalio-E-INK4-Plus
 *   main/boards/metalio-e-ink4-plus/config.h
 *
 * Upstream is MIT licensed:
 *   Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
 *   Copyright (c) 2025 Project Contributors
 *
 * DO NOT edit values here without re-checking upstream `config.h`. Several pins
 * moved between hardware revisions (V1.2 -> V1.3/V1.4); the values below are the
 * ACTIVE block upstream, i.e. V1.3/V1.4. Where upstream keeps a superseded value
 * in a comment, it is reproduced here as a `_V12` note so a future diff against
 * a rev-1.2 board is possible.
 *
 * Target SoC: ESP32-S31 (RISC-V), NOT ESP32-S3. Pins 54..61 only exist on S31.
 */

#ifndef CROSSMETAL_METALIO_PINOUT_H
#define CROSSMETAL_METALIO_PINOUT_H

#include <driver/gpio.h>

/* ------------------------------------------------------------------------- */
/* Panel geometry                                                            */
/* ------------------------------------------------------------------------- */

/* ED047TC2_1216: native 1216x684 (landscape); logical UI is portrait 684x1216. */
#define METALIO_DISPLAY_WIDTH  684
#define METALIO_DISPLAY_HEIGHT 1216

/* UI is inset 2 px on all four sides; wallpaper/standby use the full panel. */
#define METALIO_CONTENT_LEFT_INSET   2
#define METALIO_CONTENT_RIGHT_INSET  2
#define METALIO_CONTENT_TOP_INSET    2
#define METALIO_CONTENT_BOTTOM_INSET 2
#define METALIO_WHITE_BORDER_PIXELS  0

#define METALIO_CONTENT_W \
    (METALIO_DISPLAY_WIDTH - METALIO_CONTENT_LEFT_INSET - METALIO_CONTENT_RIGHT_INSET)
#define METALIO_CONTENT_H \
    (METALIO_DISPLAY_HEIGHT - METALIO_CONTENT_TOP_INSET - METALIO_CONTENT_BOTTOM_INSET)

#define METALIO_TOUCH_OFFSET_X 0
#define METALIO_TOUCH_OFFSET_Y 0

/* Panel backplane bias, millivolts. */
#define METALIO_EPD_VCOM_MV 1200

/* ------------------------------------------------------------------------- */
/* EPD parallel bus (schematic V1.2/V1.3/V1.4)                                */
/* ------------------------------------------------------------------------- */

#define EPD_PIN_D0  GPIO_NUM_8
#define EPD_PIN_D1  GPIO_NUM_9
#define EPD_PIN_D2  GPIO_NUM_10
#define EPD_PIN_D3  GPIO_NUM_11
#define EPD_PIN_D4  GPIO_NUM_12
#define EPD_PIN_D5  GPIO_NUM_13
#define EPD_PIN_D6  GPIO_NUM_14
#define EPD_PIN_D7  GPIO_NUM_15

#define EPD_PIN_XCL  GPIO_NUM_16   /* clock        */
#define EPD_PIN_XLE  GPIO_NUM_17   /* latch enable */
#define EPD_PIN_XOE  GPIO_NUM_18   /* output enable */

/* Moved on V1.3/V1.4; V1.2 used GPIO19 for XSTL and GPIO36 for MODE. */
#define EPD_PIN_XSTL_V12 GPIO_NUM_19
#define EPD_PIN_XSTL     GPIO_NUM_4    /* start pulse, V1.3/V1.4 */
#define EPD_PIN_MODE_V12 GPIO_NUM_36   /* V1.2 only; 36 is now ANA_SW */
#define EPD_PIN_MODE     GPIO_NUM_19   /* V1.4 */

#define EPD_PIN_CKV    GPIO_NUM_40
#define EPD_PIN_SPV    GPIO_NUM_35
#define EPD_PIN_BORDER GPIO_NUM_37

/* ------------------------------------------------------------------------- */
/* TPS65185 panel PMIC — controlled through the TCA9555, not ESP GPIOs.        */
/* Numbering is the datasheet P0x/P1x, encoded as (port*8 + bit).              */
/* ------------------------------------------------------------------------- */

#define EPD_TPS_IO_VCOM_CTRL 0   /* P0.0 */
#define EPD_TPS_IO_PWRUP     1   /* P0.1 */
#define EPD_TPS_IO_WAKEUP    2   /* P0.2 */
#define EPD_TPS_IO_PWR_GOOD  7   /* P0.7 */

/* Port1 only; note P1.7 is the power-button input, not a TPS interrupt. */
#define EPD_TPS_IO_INT       15  /* P1.7 */

/* ------------------------------------------------------------------------- */
/* Other TCA9555 outputs                                                      */
/* ------------------------------------------------------------------------- */

#define EPD_IO_MOTOR        3   /* P0.3 vibration motor, active high */
#define EPD_IO_PA_EN        4   /* P0.4 NS4150 amplifier enable, active high */
#define EPD_IO_CAM_SCR_EN   5   /* P0.5 camera/screen rail, active high */
#define EPD_IO_TP_RST       10  /* P1.2 touch RST, active high (low = off) */
#define EPD_IO_PWR_PULSE    11  /* P1.3 shutdown pulse, idle low */
#define EPD_IO_BT_PA_PWR    12  /* P1.4 BT + PA shared rail, active high */
#define EPD_IO_RTC_INT      14  /* P1.6 PCF8563 interrupt input */

/* ------------------------------------------------------------------------- */
/* Camera (DVP, GC2145) — outside the reader scope but wired on the same bus.  */
/* ------------------------------------------------------------------------- */

#define METALIO_CAM_SCCB_ADDR 0x3C
#define METALIO_CAM_PIN_D0    GPIO_NUM_46
#define METALIO_CAM_PIN_PCLK  GPIO_NUM_54
#define METALIO_CAM_PIN_MCLK  GPIO_NUM_55
#define METALIO_CAM_PIN_VSYNC GPIO_NUM_56
#define METALIO_CAM_PIN_HREF  GPIO_NUM_57
#define METALIO_CAM_MCLK_HZ   20000000

/* GC2145 RST shares the touch RST net; only ever software-reset the sensor. */
#define METALIO_CAM_PIN_RST   EPD_IO_TP_RST

#define METALIO_VIBRATION_MOTOR_PULSE_MS 50

/* ------------------------------------------------------------------------- */
/* I2C — one shared bus for every on-board peripheral                          */
/* ------------------------------------------------------------------------- */

#define METALIO_I2C_SDA_PIN          GPIO_NUM_0
#define METALIO_I2C_SCL_PIN          GPIO_NUM_1
#define METALIO_IO_EXPANDER_INT_GPIO GPIO_NUM_2

#define METALIO_PCF8563_I2C_ADDR     0x51
#define METALIO_SC7A20H_I2C_ADDR     0x19
#define METALIO_SC7A20H_I2C_ADDR_ALT 0x18

/* Rev 1.4: the PCF8563 interrupt goes through TCA P1.6, not an ESP GPIO. */
#define METALIO_RTC_INT_GPIO GPIO_NUM_NC

/* ------------------------------------------------------------------------- */
/* Touch — FT6336U                                                            */
/* ------------------------------------------------------------------------- */

#define METALIO_TOUCH_I2C_ADDR 0x38
#define METALIO_TOUCH_INT_PIN  GPIO_NUM_5

/* Rev 1.4 moved the analog switch off TCA P1.6 onto ESP GPIO36. */
#define METALIO_ANA_SW_GPIO GPIO_NUM_36

/* ------------------------------------------------------------------------- */
/* Cover-glass virtual keys                                                   */
/* The FT6336U reports these at a Y outside the visible panel.                 */
/* ------------------------------------------------------------------------- */

#define METALIO_TOUCH_VK_Y      1600
#define METALIO_TOUCH_VK_TOL    30
#define METALIO_TOUCH_VK_HOME_X 100
#define METALIO_TOUCH_VK_PREV_X 200   /* back / previous page */
#define METALIO_TOUCH_VK_NEXT_X 300   /* next page */
#define METALIO_TOUCH_VK_LONG_MS 500

/* ------------------------------------------------------------------------- */
/* Buttons                                                                    */
/* ------------------------------------------------------------------------- */

#define METALIO_BOOT_BUTTON_GPIO GPIO_NUM_61

#define EPD_IO_POWER     15   /* P1.7 power key */
#define EPD_IO_VOL_UP     8   /* P1.0 volume + */
#define EPD_IO_VOL_DOWN   9   /* P1.1 volume - */

#define METALIO_POWER_LONG_PRESS_MS   3000
#define METALIO_PWR_PULSE_INTERVAL_MS 100
#define METALIO_PWR_PULSE_COUNT       15

/* ------------------------------------------------------------------------- */
/* Frontlight — YX6016N EN/PWM                                                */
/* ------------------------------------------------------------------------- */

#define METALIO_FL_COOL_PIN GPIO_NUM_6
#define METALIO_FL_WARM_PIN GPIO_NUM_7

/* ------------------------------------------------------------------------- */
/* microSD — SDMMC slot 0, 4-bit                                              */
/*                                                                            */
/* These are the U5-dedicated SDIO IOMUX pads. The schematic package pin       */
/* numbers are NOT the GPIO numbers; upstream config.h says so explicitly.     */
/* ------------------------------------------------------------------------- */

#define METALIO_SDMMC_CLK_PIN GPIO_NUM_24   /* pkg 32 */
#define METALIO_SDMMC_CMD_PIN GPIO_NUM_25   /* pkg 33 */
#define METALIO_SDMMC_D0_PIN  GPIO_NUM_20   /* pkg 27 */
#define METALIO_SDMMC_D1_PIN  GPIO_NUM_21   /* pkg 28 */
#define METALIO_SDMMC_D2_PIN  GPIO_NUM_22   /* pkg 29 */
#define METALIO_SDMMC_D3_PIN  GPIO_NUM_23   /* pkg 31 */

/* ------------------------------------------------------------------------- */
/* Audio — BH1098G over I2S (ESP is slave) + UART AT                          */
/* ------------------------------------------------------------------------- */

#define METALIO_AUDIO_INPUT_SAMPLE_RATE  16000
#define METALIO_AUDIO_OUTPUT_SAMPLE_RATE 16000

#define METALIO_AUDIO_I2S_BCLK GPIO_NUM_42
#define METALIO_AUDIO_I2S_WS   GPIO_NUM_43
#define METALIO_AUDIO_I2S_DOUT GPIO_NUM_44
#define METALIO_AUDIO_I2S_DIN  GPIO_NUM_45

#define METALIO_BT_AUDIO_TX_PIN GPIO_NUM_39
#define METALIO_BT_AUDIO_RX_PIN GPIO_NUM_38

/* ------------------------------------------------------------------------- */
/* NT26 cellular modem (UART) — present in the pin map                        */
/* ------------------------------------------------------------------------- */

#define METALIO_NT26_TX_PIN   GPIO_NUM_58
#define METALIO_NT26_RX_PIN   GPIO_NUM_59
#define METALIO_NT26_MRDY_PIN GPIO_NUM_60
#define METALIO_NT26_SRDY_PIN GPIO_NUM_3

/* ------------------------------------------------------------------------- */
/* I2C slave addresses on the shared bus                                      */
/* ------------------------------------------------------------------------- */

#define METALIO_TCA9555_I2C_ADDR  0x20
#define METALIO_TOUCH_ADDR        0x38
#define METALIO_RTC_ADDR          0x51
#define METALIO_BQ27220_I2C_ADDR  0x55
#define METALIO_CX25601N_I2C_ADDR 0x6B

/* ------------------------------------------------------------------------- */
/* Helpers for the (port, bit) encoding used by the TCA9555 defines above      */
/* ------------------------------------------------------------------------- */

#define METALIO_IO_BIT(io)  (1u << ((unsigned)(io) & 7u))
#define METALIO_IO_PORT(io) ((int)((unsigned)(io) >> 3))

#endif /* CROSSMETAL_METALIO_PINOUT_H */