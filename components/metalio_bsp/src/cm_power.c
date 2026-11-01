/**
 * @file cm_power.c
 * @brief CrossMetal gauge + charger implementation.
 *
 * Ported from Metalio-E-INK4-Plus `common/power/bq27220_gauge.cc` and
 * `common/power/cx25601n.cc` (MIT).
 *
 * Deliberate deviations from upstream, both to keep this component free of the
 * Metalio settings/NVS layer:
 *   - The charge-current default is a compile-time constant here (1000 mA)
 *     instead of reading a user setting.
 *   - The gauge's SOC filters live in file-scope statics rather than a class,
 *     since CrossMetal instantiates exactly one gauge.
 */

#include "cm_power.h"

#include "cm_i2c.h"
#include "metalio_pinout.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>

static const char *TAG = "cm_power";

#define CM_POWER_LOCK_MS 100
#define CM_POWER_IO_MS 500

/* --- BQ27220 ----------------------------------------------------------------- */

#define BQ27220_REG_VOLTAGE 0x08 /* mV,  u16 LE */
#define BQ27220_REG_CURRENT 0x0C /* mA,  i16 LE */

/* SOC is smoothed over this many samples before it reaches the UI. */
#define CM_SOC_FILTER_SIZE 60

/* The displayed percentage may only move one step per this interval. */
#define CM_SOC_STEP_US (30LL * 1000 * 1000)

/* "100%" additionally requires the pack to sit above this for a full hold. */
#define CM_SOC_FULL_CONFIRM_MV 4330
#define CM_SOC_FULL_EXIT_MV 4280
#define CM_SOC_FULL_HOLD_US (30LL * 1000 * 1000)

static bool s_gauge_ready;
static bool s_charger_ready;

static int s_consecutive_err;
static float s_filter_buf[CM_SOC_FILTER_SIZE];
static float s_filter_sum;
static int s_filter_count;
static int s_filter_idx;
static bool s_filter_primed;
static int s_displayed_soc = -1;
static int64_t s_last_soc_step_us;
static int64_t s_full_above_since_us;
static int s_last_charge_dir;

/* --- CX25601N ---------------------------------------------------------------- */

#define CX25601N_REG_ICHG_LO   0x02
#define CX25601N_REG_ICHG_HI   0x03
#define CX25601N_REG_CHG_CTRL1 0x16

/* --- low level --------------------------------------------------------------- */

static bool power_read_u16(uint8_t addr, uint8_t reg, uint16_t *out) {
    uint8_t buf[2] = {0};
    if (!cm_i2c_lock(CM_POWER_LOCK_MS)) {
        return false;
    }
    const esp_err_t err = cm_i2c_read_reg_stop_locked(addr, reg, buf, sizeof(buf),
                                                      CM_POWER_IO_MS);
    cm_i2c_unlock();
    if (err != ESP_OK) {
        return false;
    }
    /* Both chips report little-endian 16-bit values. */
    *out = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return true;
}

static esp_err_t power_update_bits(uint8_t addr, uint8_t reg, uint8_t mask,
                                   uint8_t shift, uint8_t value) {
    if (!cm_i2c_lock(CM_POWER_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t cur = 0;
    esp_err_t err = cm_i2c_read_reg_stop_locked(addr, reg, &cur, 1, CM_POWER_IO_MS);
    if (err == ESP_OK) {
        cur = (uint8_t)((cur & ~(mask << shift)) | ((value & mask) << shift));
        err = cm_i2c_write_reg_locked(addr, reg, &cur, 1, CM_POWER_IO_MS);
    }
    cm_i2c_unlock();
    return err;
}

/* --- public API -------------------------------------------------------------- */

void cm_power_reset_filter(void) {
    s_filter_primed = false;
    s_filter_count = 0;
    s_filter_idx = 0;
    s_filter_sum = 0;
    s_displayed_soc = -1;
    s_last_soc_step_us = 0;
    s_full_above_since_us = 0;
    s_last_charge_dir = 0;
}

bool cm_power_gauge_ready(void) { return s_gauge_ready; }
bool cm_power_charger_ready(void) { return s_charger_ready; }

static void gauge_begin(void) {
    if (s_gauge_ready) {
        return;
    }
    if (!cm_i2c_lock(CM_POWER_LOCK_MS)) {
        return;
    }
    const esp_err_t err = cm_i2c_probe_locked(METALIO_BQ27220_I2C_ADDR, 50);
    cm_i2c_unlock();
    if (err == ESP_OK) {
        s_gauge_ready = true;
        s_consecutive_err = 0;
        cm_power_reset_filter();
        ESP_LOGI(TAG, "BQ27220 ready @0x%02X", METALIO_BQ27220_I2C_ADDR);
    } else {
        ESP_LOGW(TAG, "BQ27220 not found @0x%02X", METALIO_BQ27220_I2C_ADDR);
    }
}

bool cm_power_voltage_mv(uint16_t *mv) {
    if (!s_gauge_ready || mv == NULL) {
        return false;
    }
    uint16_t raw = 0;
    if (!power_read_u16(METALIO_BQ27220_I2C_ADDR, BQ27220_REG_VOLTAGE, &raw)) {
        if (++s_consecutive_err >= 5) {
            s_gauge_ready = false;
        }
        return false;
    }
    s_consecutive_err = 0;
    *mv = raw;
    return true;
}

bool cm_power_current_ma(int16_t *ma) {
    if (!s_gauge_ready || ma == NULL) {
        return false;
    }
    uint16_t raw = 0;
    if (!power_read_u16(METALIO_BQ27220_I2C_ADDR, BQ27220_REG_CURRENT, &raw)) {
        return false;
    }
    *ma = (int16_t)raw;
    return true;
}

static float filter_push(float sample) {
    if (!s_filter_primed) {
        for (int i = 0; i < CM_SOC_FILTER_SIZE; ++i) {
            s_filter_buf[i] = sample;
        }
        s_filter_sum = sample * CM_SOC_FILTER_SIZE;
        s_filter_count = CM_SOC_FILTER_SIZE;
        s_filter_idx = 0;
        s_filter_primed = true;
        return sample;
    }
    s_filter_sum -= s_filter_buf[s_filter_idx];
    s_filter_buf[s_filter_idx] = sample;
    s_filter_sum += sample;
    s_filter_idx = (s_filter_idx + 1) % CM_SOC_FILTER_SIZE;
    if (s_filter_count < CM_SOC_FILTER_SIZE) {
        s_filter_count++;
    }
    return s_filter_sum / s_filter_count;
}

/*
 * Move the displayed percentage toward `target` by at most one point per
 * CM_SOC_STEP_US. Upstream added this because the raw voltage-derived value
 * jitters by several points under load, which made the UI readout flicker.
 */
static int apply_mono_step(int target_pct, bool charging) {
    const int64_t now = esp_timer_get_time();

    if (s_displayed_soc < 0) {
        s_displayed_soc = target_pct;
        s_last_soc_step_us = now;
        return s_displayed_soc;
    }

    if (target_pct == s_displayed_soc) {
        return s_displayed_soc;
    }

    /* Charging may climb immediately; otherwise rate-limit the step. */
    if (target_pct > s_displayed_soc) {
        if (!charging && (now - s_last_soc_step_us) < CM_SOC_STEP_US) {
            return s_displayed_soc;
        }
        s_displayed_soc++;
        s_last_soc_step_us = now;
    } else {
        if ((now - s_last_soc_step_us) < CM_SOC_STEP_US) {
            return s_displayed_soc;
        }
        s_displayed_soc--;
        s_last_soc_step_us = now;
    }
    return s_displayed_soc;
}

bool cm_power_battery_level(int *level, bool *charging, bool *discharging) {
    if (level == NULL || charging == NULL || discharging == NULL) {
        return false;
    }
    if (!s_gauge_ready) {
        /* Self-heal: the gauge can drop off the bus after a brown-out. */
        gauge_begin();
        if (!s_gauge_ready) {
            return false;
        }
    }

    uint16_t mv = 0;
    if (!cm_power_voltage_mv(&mv) || mv < 3000) {
        return false;
    }

    /*
     * Voltage-linear approximation over a single Li-ion cell.
     * Anchored on the pack's usable window: 3.30 V empty, 4.20 V full.
     */
    float pct = (float)(mv - 3300) / (float)(4200 - 3300) * 100.0f;
    if (pct < 0.0f) {
        pct = 0.0f;
    }
    if (pct > 100.0f) {
        pct = 100.0f;
    }
    const float smooth = filter_push(pct);

    int16_t ma = 0;
    const bool have_current = cm_power_current_ma(&ma);
    const bool is_charging = have_current && ma > 5;
    const bool is_discharging = have_current && ma < -5;

    const int64_t now = esp_timer_get_time();
    int target = (int)(smooth + 0.5f);

    /*
     * Reaching 100% requires a sustained high-voltage hold, because a pack
     * sitting on the charger reads full voltage well before it is full.
     */
    if (mv >= CM_SOC_FULL_CONFIRM_MV && is_charging) {
        if (s_full_above_since_us == 0) {
            s_full_above_since_us = now;
        }
        if ((now - s_full_above_since_us) < CM_SOC_FULL_HOLD_US) {
            target = 99;
        }
    } else {
        s_full_above_since_us = 0;
        if (mv < CM_SOC_FULL_EXIT_MV && target >= 100) {
            target = 99;
        }
    }

    const int dir = is_charging ? 1 : (is_discharging ? -1 : 0);
    s_last_charge_dir = dir;

    *level = apply_mono_step(target, is_charging);
    /* Report charging only below full, so the UI can hide the bolt. */
    *charging = is_charging && *level < 100;
    *discharging = is_discharging;
    return true;
}

esp_err_t cm_power_set_charge_current_ma(uint32_t ma) {
    if (!s_charger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ma < 80) ma = 80;
    if (ma > 3040) ma = 3040;

    uint32_t code = ma / 80;
    if (code < 1) code = 1;
    if (code > 38) code = 38;

    /* The field is split across two registers: 2 bits low, 4 bits high. */
    esp_err_t err = power_update_bits(METALIO_CX25601N_I2C_ADDR, CX25601N_REG_ICHG_LO,
                                      0x03, 6, (uint8_t)(code & 0x03));
    if (err == ESP_OK) {
        err = power_update_bits(METALIO_CX25601N_I2C_ADDR, CX25601N_REG_ICHG_HI,
                                0x0F, 0, (uint8_t)((code >> 2) & 0x0F));
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "charge current set to %u mA (code %u)", code * 80u,
                 (unsigned)code);
    } else {
        ESP_LOGW(TAG, "set charge current: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t cm_power_enable_charge(bool enable) {
    if (!s_charger_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    if (enable) {
        /* Clear the termination latch, then assert the charge enable bit. */
        err = power_update_bits(METALIO_CX25601N_I2C_ADDR, CX25601N_REG_CHG_CTRL1,
                                0x01, 4, 0);
        if (err == ESP_OK) {
            err = power_update_bits(METALIO_CX25601N_I2C_ADDR, CX25601N_REG_CHG_CTRL1,
                                    0x01, 5, 1);
        }
    } else {
        err = power_update_bits(METALIO_CX25601N_I2C_ADDR, CX25601N_REG_CHG_CTRL1,
                                0x01, 5, 0);
    }
    return err;
}

void cm_power_init(void) {
    /* The gauge goes first: the charger path and the UI both read it. */
    gauge_begin();

    if (!cm_i2c_lock(CM_POWER_LOCK_MS)) {
        ESP_LOGE(TAG, "i2c lock failed during charger probe");
        return;
    }
    const esp_err_t probe = cm_i2c_probe_locked(METALIO_CX25601N_I2C_ADDR, 50);
    cm_i2c_unlock();

    if (probe != ESP_OK) {
        ESP_LOGI(TAG, "CX25601N not present @0x%02X; skipping charger init",
                 METALIO_CX25601N_I2C_ADDR);
        return;
    }
    s_charger_ready = true;

    /* Board default, matching upstream: 1000 mA fast charge. */
    const esp_err_t err = cm_power_set_charge_current_ma(1000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "default charge current: %s", esp_err_to_name(err));
    }
    cm_power_enable_charge(true);

    ESP_LOGI(TAG, "CX25601N ready @0x%02X", METALIO_CX25601N_I2C_ADDR);
}