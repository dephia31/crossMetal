/**
 * @file cm_frontlight.c
 * @brief CrossMetal frontlight implementation.
 *
 * Ported from Metalio-E-INK4-Plus `periph/frontlight.cc` (MIT).
 *
 * Upstream animates brightness with an esp_timer ramp. That is dropped here:
 * a reader either wants the light on at a set level or off, and the ramp cost a
 * timer plus per-tick state for no functional gain. Everything that actually
 * touches the hardware — the 20% output cap, the 10-bit duty, the warm/cool
 * channel split — is carried over unchanged.
 */

#include "cm_frontlight.h"

#include "metalio_pinout.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>

static const char *TAG = "cm_frontlight";

#define FL_LEDC_MODE LEDC_LOW_SPEED_MODE
#define FL_LEDC_TIMER LEDC_TIMER_0
#define FL_CH_COOL LEDC_CHANNEL_0
#define FL_CH_WARM LEDC_CHANNEL_1
#define FL_LEDC_RES LEDC_TIMER_10_BIT
#define FL_DUTY_MAX 1023
#define FL_PWM_FREQ_HZ 20000

/*
 * Hardware safety cap. UI brightness 100 maps to 20% of the PWM period; running
 * these LEDs at full duty overheats the light guide.
 */
#define FL_OUTPUT_MAX_PERCENT 20

static bool s_initialized;
static cm_fl_drive_t s_drive_mode = CM_FL_DRIVE_PWM;
static cm_fl_cct_t s_cct = CM_FL_CCT_OFF;
static uint8_t s_brightness;

static uint32_t brightness_to_duty(uint8_t brightness) {
    if (brightness > 100) {
        brightness = 100;
    }
    const uint32_t span = (FL_DUTY_MAX + 1) * FL_OUTPUT_MAX_PERCENT / 100;
    return (uint32_t)brightness * span / 100;
}

static void apply_output(void) {
    if (!s_initialized) {
        return;
    }

    const bool on = (s_cct != CM_FL_CCT_OFF) && (s_brightness > 0);
    const uint32_t duty = on ? brightness_to_duty(s_brightness) : 0;

    const bool cool_on = on && (s_cct == CM_FL_CCT_COOL || s_cct == CM_FL_CCT_BOTH);
    const bool warm_on = on && (s_cct == CM_FL_CCT_WARM || s_cct == CM_FL_CCT_BOTH);

    if (s_drive_mode == CM_FL_DRIVE_GPIO) {
        gpio_set_level(METALIO_FL_COOL_PIN, cool_on ? 1 : 0);
        gpio_set_level(METALIO_FL_WARM_PIN, warm_on ? 1 : 0);
        return;
    }

    ledc_set_duty(FL_LEDC_MODE, FL_CH_COOL, cool_on ? duty : 0);
    ledc_update_duty(FL_LEDC_MODE, FL_CH_COOL);
    ledc_set_duty(FL_LEDC_MODE, FL_CH_WARM, warm_on ? duty : 0);
    ledc_update_duty(FL_LEDC_MODE, FL_CH_WARM);
}

static bool setup_hardware(void) {
    if (s_drive_mode == CM_FL_DRIVE_GPIO) {
        const gpio_config_t conf = {
            .pin_bit_mask = (1ULL << METALIO_FL_COOL_PIN) | (1ULL << METALIO_FL_WARM_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&conf) != ESP_OK) {
            return false;
        }
        gpio_set_level(METALIO_FL_COOL_PIN, 0);
        gpio_set_level(METALIO_FL_WARM_PIN, 0);
        return true;
    }

    const ledc_timer_config_t timer = {
        .speed_mode = FL_LEDC_MODE,
        .duty_resolution = FL_LEDC_RES,
        .timer_num = FL_LEDC_TIMER,
        .freq_hz = FL_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s", esp_err_to_name(err));
        return false;
    }

    const gpio_num_t pins[2] = {METALIO_FL_COOL_PIN, METALIO_FL_WARM_PIN};
    const ledc_channel_t channels[2] = {FL_CH_COOL, FL_CH_WARM};

    for (int i = 0; i < 2; ++i) {
        const ledc_channel_config_t ch = {
            .gpio_num = pins[i],
            .speed_mode = FL_LEDC_MODE,
            .channel = channels[i],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = FL_LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&ch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config(%d): %s", (int)pins[i],
                     esp_err_to_name(err));
            return false;
        }
    }
    return true;
}

static void teardown_hardware(void) {
    if (s_drive_mode == CM_FL_DRIVE_GPIO) {
        gpio_set_level(METALIO_FL_COOL_PIN, 0);
        gpio_set_level(METALIO_FL_WARM_PIN, 0);
        gpio_reset_pin(METALIO_FL_COOL_PIN);
        gpio_reset_pin(METALIO_FL_WARM_PIN);
        return;
    }
    ledc_stop(FL_LEDC_MODE, FL_CH_COOL, 0);
    ledc_stop(FL_LEDC_MODE, FL_CH_WARM, 0);
    ledc_timer_pause(FL_LEDC_MODE, FL_LEDC_TIMER);
}

esp_err_t cm_frontlight_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }
    if (!setup_hardware()) {
        return ESP_FAIL;
    }
    s_initialized = true;
    s_cct = CM_FL_CCT_OFF;
    s_brightness = 0;
    apply_output();
    ESP_LOGI(TAG, "init: cool=GPIO%d warm=GPIO%d %d Hz, output capped at %d%%",
             METALIO_FL_COOL_PIN, METALIO_FL_WARM_PIN, FL_PWM_FREQ_HZ,
             FL_OUTPUT_MAX_PERCENT);
    return ESP_OK;
}

esp_err_t cm_frontlight_set_drive_mode(cm_fl_drive_t mode) {
    if (s_initialized && mode == s_drive_mode) {
        return ESP_OK;
    }
    if (s_initialized) {
        teardown_hardware();
        s_initialized = false;
    }
    s_drive_mode = mode;
    if (!setup_hardware()) {
        return ESP_FAIL;
    }
    s_initialized = true;
    apply_output();
    return ESP_OK;
}

void cm_frontlight_set_brightness(uint8_t brightness) {
    if (brightness > 100) {
        brightness = 100;
    }
    s_brightness = brightness;
    apply_output();
}

uint8_t cm_frontlight_brightness(void) { return s_brightness; }

void cm_frontlight_set_cct(cm_fl_cct_t cct) {
    if (cct < CM_FL_CCT_WARM || cct > CM_FL_CCT_OFF) {
        cct = CM_FL_CCT_OFF;
    }
    s_cct = cct;
    apply_output();
}

cm_fl_cct_t cm_frontlight_cct(void) { return s_cct; }

void cm_frontlight_toggle_cct(void) {
    cm_fl_cct_t next;
    switch (s_cct) {
        case CM_FL_CCT_WARM: next = CM_FL_CCT_COOL; break;
        case CM_FL_CCT_COOL: next = CM_FL_CCT_BOTH; break;
        case CM_FL_CCT_BOTH: next = CM_FL_CCT_OFF; break;
        default: next = CM_FL_CCT_WARM; break;
    }
    cm_frontlight_set_cct(next);
}

void cm_frontlight_force_off(void) {
    s_brightness = 0;
    apply_output();
}