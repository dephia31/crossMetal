/**
 * @file cm_touch.h
 * @brief CrossMetal FT6336U capacitive touch.
 *
 * Ported from Metalio-E-INK4-Plus `display/metalio_touch.c` (MIT).
 *
 * The controller reports in the panel's native landscape frame (1216x684). The
 * UI is portrait 684x1216, so every sample is rotated:
 *
 *     logical_x = raw_y
 *     logical_y = 683 - raw_x
 *
 * This is the inverse of the display's own
 * `epd_logical_to_physical(x, y) -> (y, 683 - x)`, which is why the two agree.
 *
 * The cover glass also carries three virtual keys that report at a Y coordinate
 * outside the visible panel (Y ~ 1600). They are surfaced separately so the UI
 * can map them to back / page-up / page-down without treating them as taps.
 */

#ifndef CROSSMETAL_CM_TOUCH_H
#define CROSSMETAL_CM_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool pressed;
    int x;     /* logical portrait X, 0..683 */
    int y;     /* logical portrait Y, 0..1215 */
    int raw_x; /* controller frame, for diagnostics */
    int raw_y;
} cm_touch_point_t;

/** Cover-glass virtual keys. */
typedef enum {
    CM_VK_NONE = 0,
    CM_VK_HOME,
    CM_VK_PREV,
    CM_VK_NEXT,
} cm_vk_t;

/** Called from the sampling task on every state change. */
typedef void (*cm_touch_cb_t)(const cm_touch_point_t *pt, bool pressed, cm_vk_t vk);

/** Pulse the touch reset, probe the chip and start sampling. */
bool cm_touch_init(void);

/** True once the controller ACKed and sampling is running. */
bool cm_touch_ok(void);

/**
 * Latest point.
 * @return true while a finger is down.
 */
bool cm_touch_read(cm_touch_point_t *out);

/** Register a callback for press/release edges. Pass NULL to clear. */
void cm_touch_set_callback(cm_touch_cb_t cb);

/**
 * Classify a raw sample against the virtual-key band.
 * Returns CM_VK_NONE for ordinary in-panel taps.
 */
cm_vk_t cm_touch_classify_vk(int raw_x, int raw_y);

/**
 * Pause sampling. Nested calls are counted, so the last unpause wins.
 * Used while the shared I2C bus is being drained for a flash write.
 */
void cm_touch_set_feed_paused(bool paused);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_TOUCH_H */