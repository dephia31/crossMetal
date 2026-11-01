/**
 * @file metalio_sd.h
 * @brief Compatibility shim: upstream `metalio_sd.h` -> `cm_storage.h`.
 *
 * The vendored display glue only calls `MetalioSd_ReleaseHold()` — releasing any
 * lingering gpio_hold before the panel bus is driven low. Everything else in the
 * upstream SD module belongs to the storage subsystem and lives in `cm_storage`.
 */

#ifndef CROSSMETAL_SHIM_METALIO_SD_H
#define CROSSMETAL_SHIM_METALIO_SD_H

#include "cm_storage.h"

static inline void MetalioSd_ReleaseHold(void) { cm_sd_release_hold(); }

#endif /* CROSSMETAL_SHIM_METALIO_SD_H */