/**
 * @file cm_rtc.h
 * @brief CrossMetal PCF8563 real-time clock.
 *
 * Ported from Metalio-E-INK4-Plus `periph/metalio_rtc.{cc,h}` (MIT).
 *
 * The PCF8563 sits at 0x51 on the shared bus. On hardware rev 1.4 its interrupt
 * is wired to TCA9555 P1.6 rather than to an ESP GPIO, so the INT state is read
 * by polling the expander's input register — there is no edge interrupt for it.
 */

#ifndef CROSSMETAL_CM_RTC_H
#define CROSSMETAL_CM_RTC_H

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Probe the PCF8563 and, if it holds a valid time, apply it to the system clock. */
void cm_rtc_init(void);

/** True once the chip has ACKed. */
bool cm_rtc_ok(void);

/** True if the RTC has an uncleared interrupt pending. */
bool cm_rtc_irq_pending(void);

/** Take (and clear) a pending interrupt. */
bool cm_rtc_consume_irq(void);

/** Write the system clock into the RTC. */
bool cm_rtc_set_from_system(void);

/** Apply the RTC time to the system clock. */
bool cm_rtc_apply_to_system(void);

/** 1 when the shared INT line is low (a pending event), else 0. */
int cm_rtc_gpio_level(void);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_RTC_H */