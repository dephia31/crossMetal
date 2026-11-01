/**
 * @file cm_storage.h
 * @brief CrossMetal microSD storage.
 *
 * Ported from Metalio-E-INK4-Plus `periph/metalio_sd.{cc,h}` (MIT).
 *
 * The card is on SDMMC slot 0 in 4-bit mode. On S31 the SDIO pads come out of
 * reset claimed by the CNNT (camera/connectivity) block rather than by GPIO, so
 * releasing them to GPIO before sleep is ESP32-S31-specific work that has no S3
 * equivalent. Without it the pads stay driven and leak in deep sleep.
 */

#ifndef CROSSMETAL_CM_STORAGE_H
#define CROSSMETAL_CM_STORAGE_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Mount point for the card. */
#define CM_SD_MOUNT_POINT "/sdcard"

/** Mount the card. Returns false if no card responds. */
bool cm_sd_init(void);

/** True while the card is mounted. */
bool cm_sd_ok(void);

/** Unmount. */
bool cm_sd_unmount(void);

/** Clear any lingering gpio_hold on the SD pins, then unmount and park them low. */
bool cm_sd_hold_pins_low(void);

/** latch the SD pins low across deep sleep. */
void cm_sd_latch_for_sleep(void);

/** Release gpio_hold on the SD pins. Must run early at boot: deep-sleep latches
 *  survive a CPU reset, and a held pin breaks the next mount. */
void cm_sd_release_hold(void);

#ifdef __cplusplus
}
#endif

#endif /* CROSSMETAL_CM_STORAGE_H */