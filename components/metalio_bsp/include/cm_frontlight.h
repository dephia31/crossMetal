/**
 * @file cm_frontlight.h
 * @brief CrossMetal dual-channel frontlight.
 *
 * Ported from Metalio-E-INK4-Plus `periph/frontlight.{cc,h}` (MIT).
 *
 * Two independent LEDC PWM channels drive the cool and warm strings of the
 * YX6016N. Upstream caps the electrical output at 20% of full duty for UI
 * brightness 100 — this is a hardware safety limit, not a UI preference, and is
 * preserved here.
 */

#ifndef CROSSMETAL_CM_FRONTLIGHT_H
#define CROSSMETAL_CM_FRONTLIGHT_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Colour-temperature channel selection. */
typedef enum {
    CM_FL_CCT_WARM = 0,
    CM_FL_CCT_COOL = 1,
    CM_FL_CCT_BOTH = 2,
    CM_FL_CCT_OFF = 3,
} cm_fl_cct_t;

/** Bring up both PWM channels, output off. Safe to call twice. */
esp_err_t cm_frontlight_init(void);

/** Drive mode: LEDC PWM (default) or plain GPIO on/off. */
typedef enum {
    CM_FL_DRIVE_PWM = 0,
    CM_FL_DRIVE_GPIO = 1,
} cm_fl_drive_t;

esp_err_t cm_frontlight_set_drive_mode(cm_fl_drive_t mode);

/**
 * Set brightness 0..100 and apply it immediately.
 * The value is clamped; no transition is scheduled.
 */
void cm_frontlight_set_brightness(uint8_t brightness);

/** Current brightness, 0..100. */
uint8_t cm_frontlight_brightness(void);

/** Select which strings are lit, using the current brightness. */
void cm_frontlight_set_cct(cm_fl_cct_t cct);

/** Current selection. */
cm_fl_cct_t cm_frontlight_cct(void);

/** Cycle warm -> cool -> both -> off -> warm. */
void cm_frontlight_toggle_cct(void);

/** Kill the output immediately, keeping the channel configuration. */
void cm_frontlight_force_off(void);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_FRONTLIGHT_H */