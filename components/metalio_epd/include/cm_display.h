/**
 * @file cm_display.h
 * @brief CrossMetal display facade for the ED047TC2 parallel panel.
 *
 * Equivalent of upstream `display/metalio_epd.c`. The vendored EPDiy code does
 * the work; this layer owns the initialisation order and hands out the
 * framebuffer.
 *
 * Geometry note: EPDiy runs in native landscape (1216x684). The UI is portrait
 * 684x1216, and the two relate by the same transpose the touch driver uses:
 *
 *     physical_x = logical_y
 *     physical_y = 683 - logical_x
 *
 * Upstream deliberately keeps the driver in landscape and applies that transform
 * in the UI layer rather than setting `EPD_ROT_INVERTED_PORTRAIT`, because the
 * rotated modes change how `epd_draw_pixel` indexes the buffer. This facade
 * preserves that choice.
 */

#ifndef CROSSMETAL_CM_DISPLAY_H
#define CROSSMETAL_CM_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Refresh modes exposed to the UI. */
typedef enum {
    CM_EPD_MODE_GL16 = 0,  /**< non-flashing grayscale, the default */
    CM_EPD_MODE_GC16,      /**< flashing full grayscale, cleanest result */
    CM_EPD_MODE_DU,        /**< fast monochrome direct update */
} cm_epd_mode_t;

/**
 * Bring up the panel: EPDiy init, VCOM, rotation, waveform, power-on self-test.
 *
 * Allocates the framebuffer from PSRAM. Returns false if the panel PMIC never
 * reported PWR_GOOD, which is the normal symptom of a rail or VCOM fault.
 */
bool cm_display_init(void);

/** True once init completed and the TPS65185 came up. */
bool cm_display_ready(void);

/** Native panel width (1216) and height (684). */
int cm_display_width(void);
int cm_display_height(void);

/**
 * Framebuffer for the highlevel renderer, 4 bits per pixel.
 * `cm_hl_get_framebuffer(state)` equivalent.
 */
uint8_t *cm_display_framebuffer(void);

/** Fill the framebuffer with white without touching the panel. */
void cm_display_clear_buffer(void);

/**
 * Push a buffer region to the panel.
 *
 * @param mode        refresh mode
 * @param temperature panel temperature in Celsius; drives waveform selection
 * @return ESP_OK on success
 */
esp_err_t cm_display_flush(cm_epd_mode_t mode, int temperature);

/** Flush the whole screen area. */
esp_err_t cm_display_flush_full(cm_epd_mode_t mode, int temperature);

/** Force a full flashing clear; use sparingly, it is slow and visible. */
esp_err_t cm_display_clear_screen(void);

/** Cut panel power. Call before sleep. */
void cm_display_power_off(void);

/** Set the panel's backplane bias. */
void cm_display_set_vcom(uint16_t vcom_mv);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_DISPLAY_H */