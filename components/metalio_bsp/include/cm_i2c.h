/**
 * @file cm_i2c.h
 * @brief CrossMetal shared I2C bus.
 *
 * Ported from Metalio-E-INK4-Plus `main/boards/metalio-e-ink4-plus/bus/metalio_i2c1.{c,h}`
 * (MIT, Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.).
 *
 * One synchronous master bus on I2C_NUM_1 carries every on-board peripheral:
 * TCA9555, FT6336U, PCF8563, BQ27220, CX25601N, SC7A20H and the TPS65185.
 * Every transfer goes through `cm_i2c_lock()`; the `*_locked` functions assume
 * the caller already holds it.
 */

#ifndef CROSSMETAL_CM_I2C_H
#define CROSSMETAL_CM_I2C_H

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The single shared bus instance. */
#define CM_I2C_PORT I2C_NUM_1

/** Bus clock. Kept at 100 kHz; BQ27220 needs a long SCL wait. */
#define CM_I2C_SCL_HZ 100000

/** Take the bus mutex. Returns false on timeout. */
bool cm_i2c_lock(int timeout_ms);

/** Drop the bus mutex. */
void cm_i2c_unlock(void);

/** Bring up the bus. Idempotent for the same pin pair. */
esp_err_t cm_i2c_init(gpio_num_t sda, gpio_num_t scl);

/** Tear the bus down and release every registered device. */
esp_err_t cm_i2c_deinit(void);

/** Resolve (and cache) a device handle for a 7-bit address. */
esp_err_t cm_i2c_device_locked(uint8_t addr, i2c_master_dev_handle_t *device);

/** Probe an address. Returns ESP_OK if the device ACKs. */
esp_err_t cm_i2c_probe_locked(uint8_t addr, int timeout_ms);

esp_err_t cm_i2c_transmit_locked(uint8_t addr, const uint8_t *data, size_t len,
                                 int timeout_ms);
esp_err_t cm_i2c_receive_locked(uint8_t addr, uint8_t *data, size_t len,
                                int timeout_ms);
esp_err_t cm_i2c_transmit_receive_locked(uint8_t addr, const uint8_t *tx,
                                         size_t tx_len, uint8_t *rx, size_t rx_len,
                                         int timeout_ms);

/** Write `reg` followed by `len` bytes of `data` in one transaction. */
esp_err_t cm_i2c_write_reg_locked(uint8_t addr, uint8_t reg, const uint8_t *data,
                                  size_t len, int timeout_ms);

/** Read `len` bytes from `reg`, with a STOP between the pointer write and read. */
esp_err_t cm_i2c_read_reg_stop_locked(uint8_t addr, uint8_t reg, uint8_t *data,
                                      size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_I2C_H */