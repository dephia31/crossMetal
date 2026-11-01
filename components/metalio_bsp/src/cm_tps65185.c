/**
 * @file cm_tps65185.c
 * @brief CrossMetal TPS65185 implementation.
 *
 * Ported from Metalio-E-INK4-Plus `bus/tps65185.c` (MIT).
 *
 * Upstream's `tps_vcom_kickback()` helpers were dropped: they call into EPDiy's
 * `epd_current_board()->measure_vcom()` and are a factory calibration path, not
 * something the reader needs. Everything else round-trips the same registers.
 */

#include "cm_tps65185.h"

#include "cm_i2c.h"
#include "metalio_pinout.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "cm_tps65185";

#define CM_TPS_LOCK_MS 100
#define CM_TPS_IO_TIMEOUT_MS 1000

esp_err_t cm_tps_read_register(uint8_t reg, uint8_t *value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cm_i2c_lock(CM_TPS_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t ret = cm_i2c_read_reg_stop_locked(
        CM_TPS65185_I2C_ADDR, reg, value, 1, CM_TPS_IO_TIMEOUT_MS);
    cm_i2c_unlock();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "read reg 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t cm_tps_write_register(uint8_t reg, uint8_t value) {
    if (!cm_i2c_lock(CM_TPS_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t ret = cm_i2c_write_reg_locked(CM_TPS65185_I2C_ADDR, reg,
                                                  &value, 1, CM_TPS_IO_TIMEOUT_MS);
    cm_i2c_unlock();
    return ret;
}

esp_err_t cm_tps_probe(void) {
    if (!cm_i2c_lock(CM_TPS_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t ret = cm_i2c_probe_locked(CM_TPS65185_I2C_ADDR, 50);
    cm_i2c_unlock();
    return ret;
}

void cm_tps_set_vcom(unsigned vcom_mV) {
    /* VCOM is programmed in 10 mV steps, 9 bits across VCOM1 (LSB) / VCOM2 (MSB). */
    const unsigned val = vcom_mV / 10;

    esp_err_t err = cm_tps_write_register(TPS_REG_VCOM2, (val & 0x100) >> 8);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "VCOM2 write: %s", esp_err_to_name(err));
        return;
    }
    err = cm_tps_write_register(TPS_REG_VCOM1, val & 0xFF);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "VCOM1 write: %s", esp_err_to_name(err));
    }
}

int8_t cm_tps_read_thermistor(void) {
    cm_tps_write_register(TPS_REG_TMST1, 0x80);

    /* Poll for a completed conversion; bail out rather than spin forever. */
    for (int tries = 0; tries < 100; ++tries) {
        uint8_t val = 0;
        if (cm_tps_read_register(TPS_REG_TMST1, &val) != ESP_OK) {
            continue;
        }
        if (val & 0x20) {
            break;
        }
        if (tries == 99) {
            ESP_LOGE(TAG, "thermistor read timeout");
        }
    }

    uint8_t out = 0;
    if (cm_tps_read_register(TPS_REG_TMST_VALUE, &out) != ESP_OK) {
        return 0;
    }
    return (int8_t)out;
}

void cm_tps_debug_error(void) {
    uint8_t status = 0;
    if (cm_tps_read_register(TPS_REG_INT1, &status) != ESP_OK) {
        return;
    }
    ESP_LOGE(TAG, "INT1 (0x07) = 0x%02X", status);

    /* Bit meanings per the TPS65185 datasheet INT1 register. */
    if (status & (1 << 7)) ESP_LOGE(TAG, "VCOM short / overcurrent");
    if (status & (1 << 6)) ESP_LOGE(TAG, "VNEG (-15V) short / overcurrent");
    if (status & (1 << 5)) ESP_LOGE(TAG, "VPOS (+15V) short / overcurrent");
    if (status & (1 << 4)) ESP_LOGE(TAG, "VGL (-20V) short / overcurrent");
    if (status & (1 << 3)) ESP_LOGE(TAG, "VGH (+22V) short / overcurrent");
}