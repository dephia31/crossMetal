/**
 * @file cm_sleep.c
 * @brief Deep sleep orchestration.
 */

#include "cm_sleep.h"

#include "cm_board.h"
#include "cm_display.h"
#include "cm_keys.h"
#include "cm_storage.h"
#include "metalio_pinout.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_log.h>
#include <esp_sleep.h>

static const char *TAG = "cm_sleep";

esp_err_t cm_sleep_enter(uint32_t duration_s) {
    ESP_LOGI(TAG, "entering deep sleep: duration=%u s, wake=power_key%s",
             (unsigned)duration_s, duration_s > 0 ? "+timer" : "");

    /*
     * Mask the key scanner. This stops scanning and drives the key pins
     * idle-high so no press is fabricated on wake.
     */
    cm_keys_mask_for_sleep();
    ESP_LOGI(TAG, "key scanner masked for sleep");

    /* Unmount SD and latch its pins low. */
    cm_sd_latch_for_sleep();
    ESP_LOGI(TAG, "SD card latched for sleep");

    /*
     * Power off the display. This drops XOE, then drives PWRUP/VCOM/WAKEUP low
     * via the vendor's teardown sequence. Must happen before
     * `cm_board_ioexp_mask_for_sleep()` because the mask drives the TPS lines
     * through a different path, and we need the display driver's config_reg to
     * commit first.
     */
    cm_display_power_off();
    ESP_LOGI(TAG, "display powered off");

    /* Cut the shared rails: camera/screen first, then BT/PA. */
    cm_board_sleep_cut_rails();
    ESP_LOGI(TAG, "shared rails cut");

    /*
     * Mask both expander ports: TPS lines low, motor/PA/camera keep their
     * latched state, key pins driven idle-high, all pins become outputs to mask
     * the interrupt.
     */
    cm_board_ioexp_mask_for_sleep();
    ESP_LOGI(TAG, "expander masked for sleep");

    /* Latch the panel bus low so it doesn't float and leak current. */
    cm_board_bus_latch_for_sleep();
    ESP_LOGI(TAG, "panel bus latched low");

    /*
     * Configure wake sources. The power key is TCA P1.7, which drives GPIO2 low
     * when pressed. Use EXT1 to wake on any low pin.
     */
    if (!cm_board_ioexp_enable_power_wake_pin()) {
        ESP_LOGE(TAG, "failed to enable power-key wake pin");
        return ESP_FAIL;
    }
    const uint64_t gpio2_mask = (1ULL << GPIO_NUM_2);
    esp_sleep_enable_ext1_wakeup_io(gpio2_mask, ESP_EXT1_WAKEUP_ANY_LOW);
    ESP_LOGI(TAG, "wake source: GPIO2 (power key via TCA P1.7) on LOW");

    if (duration_s > 0) {
        esp_sleep_enable_timer_wakeup(duration_s * 1000000ULL);
        ESP_LOGI(TAG, "wake source: timer after %u seconds", (unsigned)duration_s);
    }

    ESP_LOGI(TAG, "entering deep sleep now");
    esp_deep_sleep_start();

    /* Does not return. */
    return ESP_OK;
}

void cm_sleep_restore_after_wake(void) {
    ESP_LOGI(TAG, "restoring after deep sleep wake");

    /*
     * Release all gpio_hold set before sleep. Must run before any peripheral
     * init, because held pins can prevent the PMIC boost from starting.
     */
    cm_board_boot_release_holds();

    /* Restore expander port directions and re-latch the shared rails. */
    cm_board_ioexp_restore_after_sleep();

    /* Restore the shared rails: BT/PA first, then camera/screen. */
    cm_board_sleep_restore_rails();

    /*
     * Release the panel bus hold and re-drive the control lines. The display
     * init will run later in the normal bring-up path.
     */
    cm_board_bus_restore();

    ESP_LOGI(TAG, "wake restoration complete");
}
