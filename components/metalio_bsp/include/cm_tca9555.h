/**
 * @file cm_tca9555.h
 * @brief CrossMetal TCA9555 16-bit I2C I/O expander.
 *
 * Ported from Metalio-E-INK4-Plus `bus/pca9555.{c,h}` (MIT).
 *
 * The chip is a TCA9555 (a PCA9555 with interrupt support). Everything on this
 * board that is not a direct MCU pin hangs off it: the TPS65185 control lines,
 * the vibration motor, the audio PA enable, the touch reset, the volume keys and
 * the power key. Register layout and bit masks are identical to a PCA9555.
 *
 * Pin numbering follows the datasheet: "P1.3" means Port1 bit 3. The `io`
 * arguments below use the (port * 8 + bit) encoding defined in metalio_pinout.h
 * (e.g. EPD_IO_POWER == 15 == P1.7).
 */

#ifndef CROSSMETAL_CM_TCA9555_H
#define CROSSMETAL_CM_TCA9555_H

#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CM_TCA_PIN_P00 0x0001
#define CM_TCA_PIN_P01 0x0002
#define CM_TCA_PIN_P02 0x0004
#define CM_TCA_PIN_P03 0x0008
#define CM_TCA_PIN_P04 0x0010
#define CM_TCA_PIN_P05 0x0020
#define CM_TCA_PIN_P06 0x0040
#define CM_TCA_PIN_P07 0x0080
#define CM_TCA_PIN_P10 0x0100
#define CM_TCA_PIN_P11 0x0200
#define CM_TCA_PIN_P12 0x0400
#define CM_TCA_PIN_P13 0x0800
#define CM_TCA_PIN_P14 0x1000
#define CM_TCA_PIN_P15 0x2000
#define CM_TCA_PIN_P16 0x4000
#define CM_TCA_PIN_P17 0x8000
#define CM_TCA_PIN_P0_ALL 0x00FF
#define CM_TCA_PIN_P1_ALL 0xFF00

/** Override the 7-bit address before the first call if a board straps A2A1A0. */
void cm_tca9555_set_addr(uint8_t addr_7bit);
uint8_t cm_tca9555_get_addr(void);

/** Write the direction register for one port. 1 = input, 0 = output. */
esp_err_t cm_tca9555_set_config(uint8_t config_value, int high_port);

/** Write the polarity-inversion register for one port. */
esp_err_t cm_tca9555_set_inversion(uint8_t config_value, int high_port);

/** Write the output register for one port. */
esp_err_t cm_tca9555_set_value(uint8_t config_value, int high_port);

/**
 * Read the input register for one port.
 * @param high_port 0 = Port0, 1 = Port1
 * @param out       receives the 8-bit port value
 *
 * Reading an input port is also what clears the chip's interrupt, so a poll of
 * Port0 (where PWR_GOOD lives) latches the INT line back high.
 */
esp_err_t cm_tca9555_read_input(int high_port, uint8_t *out);

/** Convenience wrapper returning 0 on failure. Prefer the _ex form. */
uint8_t cm_tca9555_read_input_raw(int high_port);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_TCA9555_H */