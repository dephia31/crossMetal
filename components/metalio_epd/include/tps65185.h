/**
 * @file tps65185.h
 * @brief Compatibility shim: upstream `tps65185.h` -> `cm_tps65185.h`.
 *
 * Upstream carries the full EPDiy-based TPS driver, including the VCOM
 * kick-back calibration helpers. Those are dropped: they call into EPDiy's
 * `epd_current_board()->measure_vcom()`, which is a factory calibration path,
 * not something the reader needs. What the board glue actually uses — the
 * register accessors and the probe — is mapped here.
 */

#ifndef CROSSMETAL_SHIM_TPS65185_H
#define CROSSMETAL_SHIM_TPS65185_H

#include "cm_tps65185.h"

/*
 * The TPS_REG_* register names are already spelled identically in
 * `cm_tps65185.h` and arrive through the include above, so they are not
 * repeated here.
 */

/** Upstream 7-bit address; matches `CM_TPS65185_I2C_ADDR`. */
#define EPDIY_TPS_ADDR CM_TPS65185_I2C_ADDR

static inline esp_err_t tps_probe(i2c_port_t port) {
    (void)port;
    return cm_tps_probe();
}

static inline esp_err_t tps_write_register(i2c_port_t port, int reg, uint8_t value) {
    (void)port;
    return cm_tps_write_register((uint8_t)reg, value);
}

static inline esp_err_t tps_read_register_ex(i2c_port_t port, int reg, uint8_t *value) {
    (void)port;
    return cm_tps_read_register((uint8_t)reg, value);
}

/**
 * Upstream's convenience reader returns 0 on failure. The shim preserves that so
 * the vendored call sites keep their existing error handling.
 */
static inline uint8_t tps_read_register(i2c_port_t port, int reg) {
    (void)port;
    uint8_t v = 0;
    cm_tps_read_register((uint8_t)reg, &v);
    return v;
}

static inline void tps_set_vcom(i2c_port_t port, unsigned vcom_mV) {
    (void)port;
    cm_tps_set_vcom(vcom_mV);
}

static inline void debug_tps_error(void) { cm_tps_debug_error(); }

#endif /* CROSSMETAL_SHIM_TPS65185_H */