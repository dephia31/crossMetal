/**
 * @file metalio_i2c1.h
 * @brief Compatibility shim: upstream `metalio_i2c1.h` -> `cm_i2c.h`.
 *
 * The vendored EPDiy board glue was written against the vendor's own I2C helper,
 * which takes an explicit port argument on every call. CrossMetal has exactly one
 * shared bus, so the port parameter is accepted and ignored.
 *
 * Binding the vendored code to `cm_i2c` rather than vendoring the vendor's bus
 * implementation is deliberate: two independent bus instances on I2C_NUM_1 would
 * both try to own the same pins and peripheral, and the second
 * `i2c_new_master_bus()` would fail or wedge the port.
 */

#ifndef CROSSMETAL_SHIM_METALIO_I2C1_H
#define CROSSMETAL_SHIM_METALIO_I2C1_H

#include "cm_i2c.h"

#include <driver/i2c_master.h> /* i2c_port_t, used by the upstream signatures */

#define EPDIY_I2C_PORT I2C_NUM_1

static inline bool metalio_i2c1_lock(int timeout_ms) {
    return cm_i2c_lock(timeout_ms);
}

static inline void metalio_i2c1_unlock(void) { cm_i2c_unlock(); }

static inline esp_err_t metalio_i2c1_init(gpio_num_t sda, gpio_num_t scl) {
    return cm_i2c_init(sda, scl);
}

static inline esp_err_t metalio_i2c1_deinit(void) { return cm_i2c_deinit(); }

static inline esp_err_t metalio_i2c1_probe_locked(i2c_port_t port, uint8_t addr,
                                                 int timeout_ms) {
    (void)port;
    return cm_i2c_probe_locked(addr, timeout_ms);
}

static inline esp_err_t metalio_i2c1_device_locked(i2c_port_t port, uint8_t addr,
                                                   i2c_master_dev_handle_t *device) {
    (void)port;
    return cm_i2c_device_locked(addr, device);
}

static inline esp_err_t metalio_i2c1_transmit_locked(i2c_port_t port, uint8_t addr,
                                                     const uint8_t *data, size_t len,
                                                     int timeout_ms) {
    (void)port;
    return cm_i2c_transmit_locked(addr, data, len, timeout_ms);
}

static inline esp_err_t metalio_i2c1_receive_locked(i2c_port_t port, uint8_t addr,
                                                    uint8_t *data, size_t len,
                                                    int timeout_ms) {
    (void)port;
    return cm_i2c_receive_locked(addr, data, len, timeout_ms);
}

static inline esp_err_t metalio_i2c1_transmit_receive_locked(
    i2c_port_t port, uint8_t addr, const uint8_t *tx, size_t tx_len, uint8_t *rx,
    size_t rx_len, int timeout_ms) {
    (void)port;
    return cm_i2c_transmit_receive_locked(addr, tx, tx_len, rx, rx_len, timeout_ms);
}

static inline esp_err_t metalio_i2c1_write_reg_locked(i2c_port_t port, uint8_t addr,
                                                      uint8_t reg, const uint8_t *data,
                                                      size_t len, int timeout_ms) {
    (void)port;
    return cm_i2c_write_reg_locked(addr, reg, data, len, timeout_ms);
}

static inline esp_err_t metalio_i2c1_read_reg_stop_locked(i2c_port_t port,
                                                          uint8_t addr, uint8_t reg,
                                                          uint8_t *data, size_t len,
                                                          int timeout_ms) {
    (void)port;
    return cm_i2c_read_reg_stop_locked(addr, reg, data, len, timeout_ms);
}

#endif /* CROSSMETAL_SHIM_METALIO_I2C1_H */