/**
 * @file cm_tps65185.h
 * @brief CrossMetal TPS65185 panel PMIC driver.
 *
 * Ported from Metalio-E-INK4-Plus `bus/tps65185.{c,h}` (MIT).
 *
 * The TPS65185 generates the panel rails (VGH/VGL/VPOS/VNEG) and the VCOM bias
 * for the ED047TC2. Its I2C address is 0x68 — note this differs from the TCA9555
 * at 0x20; the two are separate devices on the same bus. The enable/power-up
 * sequencing pins are NOT on this chip's I2C interface: they are driven from the
 * TCA9555 (see epd_board glue).
 */

#ifndef CROSSMETAL_CM_TPS65185_H
#define CROSSMETAL_CM_TPS65185_H

#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 7-bit I2C address of the TPS65185. */
#define CM_TPS65185_I2C_ADDR 0x68

#define TPS_REG_TMST_VALUE 0x00
#define TPS_REG_ENABLE     0x01
#define TPS_REG_VADJ       0x02
#define TPS_REG_VCOM1      0x03
#define TPS_REG_VCOM2      0x04
#define TPS_REG_INT_EN1    0x05
#define TPS_REG_INT_EN2    0x06
#define TPS_REG_INT1       0x07
#define TPS_REG_INT2       0x08
#define TPS_REG_UPSEQ0     0x09
#define TPS_REG_UPSEQ1     0x0A
#define TPS_REG_DWNSEQ0    0x0B
#define TPS_REG_DWNSEQ1    0x0C
#define TPS_REG_TMST1      0x0D
#define TPS_REG_TMST2      0x0E
#define TPS_REG_PG         0x0F
#define TPS_REG_REVID      0x10

esp_err_t cm_tps_write_register(uint8_t reg, uint8_t value);
esp_err_t cm_tps_read_register(uint8_t reg, uint8_t *value);

/** Returns ESP_OK when the chip ACKs its address. */
esp_err_t cm_tps_probe(void);

/**
 * Set the VCOM backplane bias in millivolts.
 * The chip stores VCOM in 10 mV units: 1200 mV -> register value 120.
 */
void cm_tps_set_vcom(unsigned vcom_mV);

/** Read the on-board thermistor through the PMIC's TMST block. */
int8_t cm_tps_read_thermistor(void);

/** Log which panel rails reported a fault in INT1. */
void cm_tps_debug_error(void);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_TPS65185_H */