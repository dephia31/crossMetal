/**
 * @file cm_keys.h
 * @brief CrossMetal button input.
 *
 * Ported from Metalio-E-INK4-Plus `input/metalio_keys.{cc,h}` (MIT).
 *
 * BOOT is a real ESP GPIO (61) and is edge-interrupt driven. POWER, VOL+ and
 * VOL- are on the TCA9555 and have no interrupt pin of their own: they raise the
 * expander's shared INT line on GPIO2, and the scanner then reads the port.
 *
 * Polarity: the expander keys rest high (internal pull-up) and read low when
 * pressed. BOOT's idle level is sampled at init rather than hardcoded, because
 * it depends on the strapping circuit.
 */

#ifndef CROSSMETAL_CM_KEYS_H
#define CROSSMETAL_CM_KEYS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Key slots, in the order used by cm_keys_is_down(). */
enum {
    CM_KEY_BOOT = 0,
    CM_KEY_VOL_UP = 1,
    CM_KEY_VOL_DOWN = 2,
    CM_KEY_POWER = 3,
    CM_KEY_COUNT = 4,
};

/** Start the BOOT GPIO interrupt and the key sampling task. */
void cm_keys_init(void);

/** True while the scanner is running (false during the sleep mask). */
bool cm_keys_ready(void);

/** Take one queued key edge (press or release). */
bool cm_keys_take_edge(void);

/** Current pressed state. `index` is one of the CM_KEY_* values. */
bool cm_keys_is_down(int index);

/** Take a pending shutdown-pulse request raised by a power long-press. */
bool cm_keys_take_shutdown_pulse(void);

/** Take a latched power short-press. Survives a blocking full refresh. */
bool cm_keys_take_power_click(void);

/** Non-consuming peek at the latched power short-press. */
bool cm_keys_has_power_click(void);

/**
 * Prepare the expander for sleep: stop scanning first, then drive the key pins
 * to their idle level and mask the interrupt.
 *
 * Order matters. Driving the pins low while the scanner still runs is read as a
 * volume-key press, and the following release then emits a spurious page turn.
 */
void cm_keys_mask_for_sleep(void);

/** Restore port directions and re-baseline the scanner after a wake. */
void cm_keys_resume_after_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_KEYS_H */