/**
 * @file cm_board.h
 * @brief CrossMetal board-level sequencing.
 *
 * Ported from Metalio-E-INK4-Plus
 * `display/epd_board_metalio_eink4_plus.c` and `.h` (MIT).
 *
 * This is the layer that ties the buses together. The panel PMIC's control
 * lines are not on the PMIC's own I2C interface — they hang off the TCA9555 — so
 * reaching the panel is a sequence across two chips:
 *
 *   board layer:   put the TPS in STANDBY (PWRUP low, WAKEUP high) so it
 *                  answers I2C, then probe it
 *   display driver: raise PWRUP and VCOM_CTRL together, wait for PWR_GOOD,
 *                  write ENABLE, then program VCOM
 *
 * The up-sequence deliberately lives in `epd_board_poweron_once()`, not here:
 * the vendored EPDiy board glue already implements the vendor's ordering, and
 * splitting it across two layers produced a boot that never reached PWR_GOOD.
 *
 * Shutdown runs the documented order in reverse: PWRUP/VCOM down, then WAKEUP
 * down, then a >=100 ms settle before the chip is allowed to sleep.
 */

#ifndef CROSSMETAL_CM_BOARD_H
#define CROSSMETAL_CM_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "metalio_pinout.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up I2C, probe the expander, and configure port directions. */
esp_err_t cm_board_init(void);

/** True once the TCA9555 has been probed and configured. */
bool cm_board_ioexp_ok(void);

/** True once the TPS65185 has ACKed. */
bool cm_board_tps_ok(void);

/**
 * Confirm the panel PMIC is present and has been placed in STANDBY.
 *
 * This does not raise the rails and does not observe PWR_GOOD; ESP_OK means the
 * expander is up and the TPS ACKed on I2C during `cm_board_init()`. Panel power
 * is owned by the display driver — see the note above `cm_board_init`.
 */
esp_err_t cm_board_display_power_on(void);

/** Pulse the touch reset on TCA P1.2 (low >=5 ms, then high, then 300 ms). */
void cm_board_touch_reset(void);

/** Vibration motor on TCA P0.3, active high. */
void cm_board_motor_set(bool on);

/** Audio amplifier enable on TCA P0.4, active high. */
void cm_board_pa_set(bool on);

/** Camera / screen rail on TCA P0.5, active high. */
void cm_board_cam_scr_set(bool on);

/** USB path analog switch on ESP GPIO36. */
void cm_board_ana_sw_set(bool high);

/** Fire the software power-off pulse train on TCA P1.3. */
void cm_board_pwr_key_pulse_train(void);

/** Read one expander pin (0..15). Returns true when high. */
bool cm_board_ioexp_get_level(int io);

/** Read one whole expander port. */
bool cm_board_ioexp_read_port(int port, uint8_t *out);

/** Wait for the expander interrupt on GPIO2. */
bool cm_board_ioexp_irq_wait(uint32_t timeout_ms);

/**
 * Take the shared P1.2 reset line (touch / camera / 4G).
 * The 4G module needs this line high, so callers must pair acquire with release.
 */
esp_err_t cm_board_shared_reset_acquire(uint32_t timeout_ms);
void cm_board_shared_reset_release(void);

/**
 * Port-direction masks, exported so the display driver can write exactly the
 * bits it owns without disturbing the ones it does not.
 *
 * The expander has a single output register per port, but its bits are owned by
 * different subsystems. Two drivers each keeping their own shadow of that
 * register is a real defect: a stale shadow in one layer overwrites the other's
 * bits, and after a display power-off a motor or camera-rail write would
 * silently re-energise the panel. All writes therefore funnel through
 * `cm_board_ioexp_write_port()` against one authoritative image.
 */
#define CM_BOARD_P0_OUT_MASK                                                  \
    ((uint8_t)(METALIO_IO_BIT(EPD_TPS_IO_VCOM_CTRL) |                         \
               METALIO_IO_BIT(EPD_TPS_IO_PWRUP) |                             \
               METALIO_IO_BIT(EPD_TPS_IO_WAKEUP) |                            \
               METALIO_IO_BIT(EPD_IO_MOTOR) | METALIO_IO_BIT(EPD_IO_PA_EN) |  \
               METALIO_IO_BIT(EPD_IO_CAM_SCR_EN)))

#define CM_BOARD_P1_OUT_MASK                                                  \
    ((uint8_t)(METALIO_IO_BIT(EPD_IO_TP_RST) |                                \
               METALIO_IO_BIT(EPD_IO_BT_PA_PWR) |                             \
               METALIO_IO_BIT(EPD_IO_PWR_PULSE)))

/**
 * Write a port through the shared image.
 *
 * @param port  0 or 1
 * @param value output bits
 * @param mask  which bits this caller owns; bits outside the mask keep their
 *              current value, so a caller cannot clobber another subsystem
 */
esp_err_t cm_board_ioexp_write_port(int port, uint8_t value, uint8_t mask);

/**
 * Retake the expander interrupt handler after the display driver has run.
 *
 * The vendored display glue attaches its own handler to GPIO2, replacing this
 * one. Call this once after display init so key scanning keeps its interrupt and
 * does not fall back to polling.
 */
void cm_board_ioexp_irq_reclaim(void);

/** Cut the shared rails before sleep: P0.5 first, then P1.4. */
void cm_board_sleep_cut_rails(void);

/** Restore the shared rails after a wake: P1.4 first, then P0.5. */
void cm_board_sleep_restore_rails(void);

/**
 * Prepare both expander ports for sleep.
 *
 * Port0 drives the TPS control lines low while the motor/PA/camera bits keep
 * their latched values. Port1 keeps the gated rails (P1.2 touch reset, P1.4
 * BT/PA) at their current level, drives the three key pins to their idle HIGH so
 * no press is fabricated, and holds P1.3 low.
 *
 * Driving the key pins low while the scanner is still running is what emits a
 * spurious page turn on wake, so stop scanning first.
 */
void cm_board_ioexp_mask_for_sleep(void);

/** Restore default port directions and re-latch the rails after a wake. */
void cm_board_ioexp_restore_after_sleep(void);

/**
 * Re-arm only the power key (P1.7) as an input so it can source a wake.
 * @return true on success.
 */
bool cm_board_ioexp_enable_power_wake_pin(void);

/** Drive every panel bus pin low and hold it. */
void cm_board_bus_hold_low(void);

/** Latched the panel bus low across sleep. */
void cm_board_bus_latch_for_sleep(void);

/** Release the bus hold and re-drive the control lines. */
void cm_board_bus_restore(void);

/**
 * Release every gpio_hold at boot.
 *
 * Deep-sleep latches survive a CPU reset, so a pin left held low last session
 * stays low and the PMIC's boost then fails to start. Must run before any
 * peripheral init.
 */
void cm_board_boot_release_holds(void);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_BOARD_H */