/**
 * @file cm_rtc.c
 * @brief CrossMetal PCF8563 implementation.
 *
 * Ported from Metalio-E-INK4-Plus `periph/metalio_rtc.cc` (MIT).
 *
 * Register map and the BCD conversion follow the PCF8563 datasheet. The VL
 * (voltage-low) flag in register 0x02 tells us whether the oscillator stopped,
 * meaning the stored time cannot be trusted.
 */

#include "cm_rtc.h"

#include "cm_i2c.h"
#include "cm_tca9555.h"
#include "metalio_pinout.h"

#include <esp_log.h>
#include <sys/time.h>

static const char *TAG = "cm_rtc";

#define CM_RTC_LOCK_MS 100
#define CM_RTC_IO_MS 500

/* Time registers 0x02..0x08 hold seconds, minutes, hours, day, weekday, month,
 * year — all BCD. Register 0x02 bit 7 is VL. */
#define PCF8563_REG_CTRL1 0x00
#define PCF8563_REG_CTRL2 0x01
#define PCF8563_REG_SEC 0x02
#define PCF8563_REG_MIN 0x03
#define PCF8563_REG_HOUR 0x04
#define PCF8563_REG_DAY 0x05
#define PCF8563_REG_WEEK 0x06
#define PCF8563_REG_MONTH 0x07
#define PCF8563_REG_YEAR 0x08

#define PCF8563_VL_FLAG 0x80

static bool s_ready;

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)(((v >> 4) & 0x0F) * 10 + (v & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t v) {
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static esp_err_t rtc_read(uint8_t reg, uint8_t *buf, size_t len) {
    if (!cm_i2c_lock(CM_RTC_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = cm_i2c_read_reg_stop_locked(METALIO_PCF8563_I2C_ADDR, reg,
                                                      buf, len, CM_RTC_IO_MS);
    cm_i2c_unlock();
    return err;
}

static esp_err_t rtc_write(uint8_t reg, const uint8_t *buf, size_t len) {
    if (!cm_i2c_lock(CM_RTC_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = cm_i2c_write_reg_locked(METALIO_PCF8563_I2C_ADDR, reg, buf,
                                                  len, CM_RTC_IO_MS);
    cm_i2c_unlock();
    return err;
}

bool cm_rtc_ok(void) { return s_ready; }

bool cm_rtc_apply_to_system(void) {
    if (!s_ready) {
        return false;
    }

    uint8_t raw[7] = {0};
    if (rtc_read(PCF8563_REG_SEC, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }

    /* A stopped oscillator invalidates the stored calendar. */
    if (raw[0] & PCF8563_VL_FLAG) {
        ESP_LOGW(TAG, "VL flag set: RTC lost time, not applying");
        return false;
    }

    struct tm t = {0};
    t.tm_sec = bcd_to_bin(raw[0] & 0x7F);
    t.tm_min = bcd_to_bin(raw[1] & 0x7F);
    t.tm_hour = bcd_to_bin(raw[2] & 0x3F);
    t.tm_mday = bcd_to_bin(raw[3] & 0x3F);
    t.tm_mon = bcd_to_bin(raw[5] & 0x1F) - 1; /* register is 1-based */
    t.tm_year = bcd_to_bin(raw[6]) + 100;     /* tm_year is since 1900 */

    if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31 ||
        t.tm_year < 100) {
        ESP_LOGW(TAG, "implausible calendar: %04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
                 t.tm_sec);
        return false;
    }

    t.tm_isdst = -1;
    const time_t epoch = mktime(&t);
    if (epoch < 0) {
        return false;
    }

    const struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
    if (settimeofday(&tv, NULL) != 0) {
        return false;
    }
    ESP_LOGI(TAG, "system clock set from PCF8563: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return true;
}

bool cm_rtc_set_from_system(void) {
    if (!s_ready) {
        return false;
    }

    const time_t now = time(NULL);
    struct tm t = {0};
    if (localtime_r(&now, &t) == NULL) {
        return false;
    }

    uint8_t raw[7];
    raw[0] = bin_to_bcd(t.tm_sec);
    raw[1] = bin_to_bcd(t.tm_min);
    raw[2] = bin_to_bcd(t.tm_hour);
    raw[3] = bin_to_bcd(t.tm_mday);
    raw[4] = bin_to_bcd(t.tm_wday);
    raw[5] = bin_to_bcd(t.tm_mon + 1);
    raw[6] = bin_to_bcd(t.tm_year % 100);

    /* Writing seconds also clears VL. */
    return rtc_write(PCF8563_REG_SEC, raw, sizeof(raw)) == ESP_OK;
}

static bool rtc_int_active(void) {
    /* PCF8563 INT is open-drain active low, routed through the expander. */
    const uint8_t p1 = cm_tca9555_read_input_raw(1);
    return (p1 & (1u << (EPD_IO_RTC_INT & 7))) == 0;
}

void cm_rtc_init(void) {
    if (!cm_i2c_lock(CM_RTC_LOCK_MS)) {
        ESP_LOGE(TAG, "i2c lock failed during RTC probe");
        return;
    }
    const esp_err_t probe = cm_i2c_probe_locked(METALIO_PCF8563_I2C_ADDR, 50);
    cm_i2c_unlock();

    if (probe != ESP_OK) {
        ESP_LOGW(TAG, "PCF8563 not present @0x%02X", METALIO_PCF8563_I2C_ADDR);
        return;
    }
    s_ready = true;

    if (cm_rtc_apply_to_system()) {
        ESP_LOGI(TAG, "boot time source: PCF8563");
    } else {
        ESP_LOGW(TAG, "PCF8563 present but time not applied");
    }
}

bool cm_rtc_irq_pending(void) {
    return s_ready && rtc_int_active();
}

bool cm_rtc_consume_irq(void) {
    if (!cm_rtc_irq_pending()) {
        return false;
    }
    /* Clear the flag registers; CTRL2 bit 7 is the AIE, and clearing it stops
     * the chip holding INT low. */
    uint8_t ctrl2 = 0;
    if (rtc_read(PCF8563_REG_CTRL2, &ctrl2, 1) == ESP_OK) {
        ctrl2 &= (uint8_t)~0x80;
        rtc_write(PCF8563_REG_CTRL2, &ctrl2, 1);
    }
    return true;
}

int cm_rtc_gpio_level(void) {
    return rtc_int_active() ? 1 : 0;
}