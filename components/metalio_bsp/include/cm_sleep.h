/**
 * @file cm_sleep.h
 * @brief Deep sleep orchestration for Metalio E-Ink 4 Plus.
 *
 * Handles the teardown and wake-up sequences across all subsystems. The sleep
 * path is order-sensitive: the panel must be powered down before the expander
 * TPS lines are driven low, and every GPIO that remains energized must be
 * latched so it survives the CPU reset.
 *
 * Wake sources:
 * - Power key (TCA P1.7 via GPIO2 EXT0)
 * - Timer (RTC_TIMER_TRIG_EN)
 */

#ifndef CROSSMETAL_CM_SLEEP_H
#define CROSSMETAL_CM_SLEEP_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enter deep sleep after preparing all subsystems.
 *
 * Sequence:
 *  1. Stop key scanner
 *  2. Unmount SD card and latch pins low
 *  3. Power off display (XOE low, then PWRUP/VCOM/WAKEUP)
 *  4. Cut shared rails (camera/screen P0.5, then BT/PA P1.4)
 *  5. Mask expander ports (TPS lines low, keys idle-high)
 *  6. Latch panel bus low
 *  7. Enable power-key wake source (TCA P1.7 → GPIO2 EXT0)
 *  8. Enter deep sleep
 *
 * @param duration_s  Wake after this many seconds (0 = wake on key only)
 * @return ESP_OK if sleep was entered (function does not return on success)
 */
esp_err_t cm_sleep_enter(uint32_t duration_s);

/**
 * Restore subsystems after a wake from deep sleep.
 *
 * This is automatically called early in boot by the main task if
 * `esp_reset_reason()` is `ESP_RST_DEEPSLEEP`. It releases gpio_hold on all
 * latched pins and restores the expander ports, but does not re-initialize
 * peripherals — that happens in the normal bring-up path.
 */
void cm_sleep_restore_after_wake(void);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_SLEEP_H */
