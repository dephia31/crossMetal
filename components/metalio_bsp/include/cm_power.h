/**
 * @file cm_power.h
 * @brief CrossMetal battery gauge and charger.
 *
 * Ported from Metalio-E-INK4-Plus `periph/metalio_power.cc` and the
 * `common/power/{bq27220_gauge,cx25601n}` drivers (MIT).
 *
 * Two chips, both on the shared I2C bus:
 *   BQ27220  @ 0x55 — fuel gauge; voltage and current, 16-bit little-endian
 *   CX25601N @ 0x6B — charger; charge current, enable/disable
 *
 * Upstream derives state-of-charge from a filtered, one-way-stepping,
 * voltage-linear model rather than coulomb counting, because the pack has no
 * usable learned capacity. That behaviour is reproduced here.
 */

#ifndef CROSSMETAL_CM_POWER_H
#define CROSSMETAL_CM_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Probe both chips, start the gauge and configure the charger (1000 mA). */
void cm_power_init(void);

/** True once the BQ27220 has ACKed at least once. */
bool cm_power_gauge_ready(void);

/** True once the CX25601N has been initialised. */
bool cm_power_charger_ready(void);

/**
 * Battery voltage through a 60-sample moving average.
 * @return false if the gauge is not readable.
 */
bool cm_power_voltage_mv(uint16_t *mv);

/** Signed current, positive when charging. */
bool cm_power_current_ma(int16_t *ma);

/**
 * State of charge plus charge direction.
 * @param level       0..100
 * @param charging    true when current > +5 mA and level < 100
 * @param discharging true when current < -5 mA
 */
bool cm_power_battery_level(int *level, bool *charging, bool *discharging);

/** Set the charger's fast-charge current. Clamped to 80..3040 mA, 80 mA steps. */
esp_err_t cm_power_set_charge_current_ma(uint32_t ma);

/** Enable or disable the charger's charging path. */
esp_err_t cm_power_enable_charge(bool enable);

/** Drop the gauge's rolling filters (call after a wake or a full charge). */
void cm_power_reset_filter(void);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_POWER_H */