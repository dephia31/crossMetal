/**
 * @file cm_tca9555.c
 * @brief CrossMetal TCA9555 implementation.
 *
 * Ported from Metalio-E-INK4-Plus `bus/pca9555.c` (MIT). Reads keep upstream's
 * STOP-between-pointer-and-read ordering, which the TPS/TCA paths depend on.
 */

#include "cm_tca9555.h"

#include "cm_i2c.h"
#include "metalio_pinout.h"

#include <esp_log.h>

static const char *TAG = "cm_tca9555";

#define CM_TCA_LOCK_MS 100

/* Register pointer addresses. */
#define REG_INPUT_PORT0  0
#define REG_OUTPUT_PORT0 2
#define REG_INVERT_PORT0 4
#define REG_CONFIG_PORT0 6

static uint8_t s_addr = METALIO_TCA9555_I2C_ADDR;

void cm_tca9555_set_addr(uint8_t addr_7bit) {
    s_addr = addr_7bit & 0x7F;
}

uint8_t cm_tca9555_get_addr(void) {
    return s_addr;
}

static esp_err_t write_single(int reg, uint8_t value) {
    if (!cm_i2c_lock(CM_TCA_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t ret =
        cm_i2c_write_reg_locked(s_addr, (uint8_t)reg, &value, 1, 50);
    cm_i2c_unlock();
    return ret;
}

esp_err_t cm_tca9555_set_config(uint8_t config_value, int high_port) {
    return write_single(REG_CONFIG_PORT0 + high_port, config_value);
}

esp_err_t cm_tca9555_set_inversion(uint8_t config_value, int high_port) {
    return write_single(REG_INVERT_PORT0 + high_port, config_value);
}

esp_err_t cm_tca9555_set_value(uint8_t config_value, int high_port) {
    return write_single(REG_OUTPUT_PORT0 + high_port, config_value);
}

esp_err_t cm_tca9555_read_input(int high_port, uint8_t *out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cm_i2c_lock(CM_TCA_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = cm_i2c_read_reg_stop_locked(
        s_addr, (uint8_t)(REG_INPUT_PORT0 + high_port), out, 1, 50);
    cm_i2c_unlock();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "read port %d: %s", high_port, esp_err_to_name(err));
    }
    return err;
}

uint8_t cm_tca9555_read_input_raw(int high_port) {
    uint8_t v = 0;
    if (cm_tca9555_read_input(high_port, &v) != ESP_OK) {
        return 0;
    }
    return v;
}