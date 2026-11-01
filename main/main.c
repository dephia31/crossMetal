/**
 * @file main.c
 * @brief CrossMetal bring-up entry point.
 *
 * This is the milestone-1 integration: it exercises every ported driver in the
 * order the vendor firmware does, and logs what answered. Once this boots on
 * hardware, the reader stack is wired on top.
 *
 * Bring-up order is taken from upstream
 * `main/boards/metalio-e-ink4-plus/metalio_e_ink4_plus_board.cc`:
 * expander and I2C first (everything else depends on them), then the panel PMIC,
 * then power, then the frontlight (early, so the first frame is not lit late),
 * then RTC, touch, keys and SD.
 */

#include "cm_board.h"
#include "cm_display.h"
#include "cm_frontlight.h"
#include "cm_keys.h"
#include "cm_power.h"
#include "cm_rtc.h"
#include "cm_storage.h"
#include "cm_touch.h"
#include "metalio_pinout.h"

/*
 * The bring-up harness draws directly against the vendored EPDiy API. The
 * reader UI will go through `cm_display.h` instead; this is scaffolding.
 */
#include "epdiy.h"
#include "epd_board_metalio_eink4_plus.h"

#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "crossmetal";

/*
 * Bytes the vendored EPDiy highlevel renderer will demand from PSRAM.
 *
 * Derived from `epd_hl_init()` in components/metalio_epd/src/highlevel.c:
 *   fb_size = epd_width() / 2 * epd_height()      (4 bpp, so width/2 bytes/row)
 *   front_fb      = fb_size
 *   back_fb       = fb_size
 *   difference_fb = 2 * fb_size                   (1 byte per pixel)
 * so the total is 4 * fb_size.
 *
 * At 1216x684 that is 1,663,488 bytes ≈ 1.59 MB — twice the naive
 * width*height*2 estimate, because the difference buffer is one full byte per
 * pixel rather than two pixels per byte.
 */
static size_t epdiy_psram_requirement(void) {
    const size_t fb_size = (size_t)(METALIO_DISPLAY_HEIGHT) * (METALIO_DISPLAY_WIDTH / 2);
    return fb_size * 4;
}

/*
 * Prove the display can actually be allocated before EPDiy tries.
 *
 * `epd_hl_init()` checks its allocations with `assert()`, and this project
 * builds with assertions at level 2, where a failure calls abort(). On a device
 * that means panic -> reboot -> panic: a boot loop with no useful message. So
 * the budget is verified here first, and a shortfall is reported plainly instead
 * of being allowed to reach the assert.
 */
static bool check_framebuffer_budget(void) {
    const size_t need = epdiy_psram_requirement();
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "display needs %u bytes of PSRAM (front+back+2x difference)",
             (unsigned)need);

    if (free_psram < need) {
        ESP_LOGE(TAG, "insufficient PSRAM for the display: need %u, have %u free",
                 (unsigned)need, (unsigned)free_psram);
        ESP_LOGE(TAG, "refusing to call epd_hl_init(); it would assert and reboot");
        return false;
    }

    /*
     * Allocate the same total in one block and release it. This proves the
     * allocator can satisfy a request of that size contiguously, which a
     * free-bytes count alone does not.
     */
    void *probe = heap_caps_aligned_alloc(64, need, MALLOC_CAP_SPIRAM);
    if (probe == NULL) {
        ESP_LOGE(TAG, "a %u-byte PSRAM block cannot be allocated; fragmentation?",
                 (unsigned)need);
        return false;
    }
    heap_caps_free(probe);
    ESP_LOGI(TAG, "framebuffer budget verified: %u bytes allocatable from PSRAM",
             (unsigned)need);
    return true;
}

static void log_chip_info(void) {
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "chip: %s, %d core(s), rev %d.%d", CONFIG_IDF_TARGET,
             chip.cores, chip.revision / 100, chip.revision % 100);

    const size_t psram = esp_psram_get_size();
    if (psram == 0) {
        ESP_LOGE(TAG, "PSRAM NOT DETECTED — the panel buffer cannot be allocated");
    } else {
        ESP_LOGI(TAG, "PSRAM: %u bytes (octal)", (unsigned)psram);
    }

    ESP_LOGI(TAG, "internal heap: %u free / %u total",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM heap:    %u free / %u total",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
}

static bool s_display_ready;

/*
 * Draw a bring-up test pattern straight into the framebuffer and push it.
 *
 * A plain white fill proves power, waveform and bus are all working. The black
 * border and diagonals then prove the geometry: they must land on all four edges
 * with no clipping or wrap, and the corner marks must be inside the panel. If the
 * image is rotated, mirrored or sheared, the transform between UI logical
 * coordinates and the panel's native landscape frame is wrong.
 */
static void draw_test_pattern(void) {
    uint8_t *fb = cm_display_framebuffer();
    if (fb == NULL) {
        ESP_LOGE(TAG, "no framebuffer to draw into");
        return;
    }

    const int w = cm_display_width();   /* native 1216 */
    const int h = cm_display_height();  /* native 684  */

    /* White background: colour 0xFF is white in 4-bpp packing. */
    epd_draw_rect(epd_full_screen(), 0xFF, fb);

    /* Black border, 4 px, so all four edges are unambiguously covered. */
    const uint8_t black = 0x00;
    epd_draw_rect((EpdRect){0, 0, w, 4}, black, fb);
    epd_draw_rect((EpdRect){0, h - 4, w, 4}, black, fb);
    epd_draw_rect((EpdRect){0, 0, 4, h}, black, fb);
    epd_draw_rect((EpdRect){w - 4, 0, 4, h}, black, fb);

    /* Diagonals: a shear or mirror shows up immediately here. */
    epd_draw_line(0, 0, w - 1, h - 1, black, fb);
    epd_draw_line(0, h - 1, w - 1, 0, black, fb);

    /* Centred cross marks the geometric middle. */
    epd_draw_hline(w / 2 - 40, h / 2, 80, black, fb);
    epd_draw_vline(w / 2, h / 2 - 40, 80, black, fb);

    ESP_LOGI(TAG, "test pattern drawn on %dx%d native framebuffer", w, h);
}

void app_main(void) {
    ESP_LOGI(TAG, "CrossMetal bring-up — Metalio E-Ink4-Plus (ESP32-S31)");
    /*
     * Printed first so a boot that dies silently leaves the reason behind on the
     * next run. The settle delay in `epd_board_init()` has no confirmed root
     * cause; ESP_RST_BROWNOUT here would point at supply sag rather than a hang.
     */
    ESP_LOGI(TAG, "reset reason: %d", (int)esp_reset_reason());
    log_chip_info();

    if (!check_framebuffer_budget()) {
        ESP_LOGE(TAG, "aborting bring-up: display memory is not viable");
        return;
    }

    /* 1. Buses and the I/O expander. Everything below depends on these. */
    const esp_err_t board = cm_board_init();
    if (board != ESP_OK) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(board));
        return;
    }

    /*
     * 2. Panel PMIC. This only confirms the TPS answered once at init and puts
     *    it in STANDBY; the display driver's power-on sequence raises the rails.
     */
    if (cm_board_display_power_on() != ESP_OK) {
        ESP_LOGW(TAG, "panel PMIC not available; display will likely fail");
    }

    /*
     * 3. Display. Runs after the board layer so the expander and PMIC are already
     *    up, because EPDiy's power sequence drives the TPS through the expander.
     */
    s_display_ready = cm_display_init();
    if (s_display_ready) {
        draw_test_pattern();
        /* 22 C is the mid-point of the waveform's library temperature range. */
        if (cm_display_flush_full(CM_EPD_MODE_GC16, 22) == ESP_OK) {
            ESP_LOGI(TAG, "test pattern flushed to panel");
        } else {
            ESP_LOGE(TAG, "flush failed; panel did not accept the frame");
        }
        /*
         * The display driver attaches its own handler to the expander's shared
         * interrupt pin. Take it back so key scanning keeps its edge wakeup.
         */
        cm_board_ioexp_irq_reclaim();
    } else {
        ESP_LOGE(TAG, "display init failed; skipping test pattern");
    }

    /* 4. Battery gauge and charger. */
    cm_power_init();

    /* 5. Frontlight, brought up early so the first frame is legible. */
    cm_frontlight_init();

    /* 6. Real-time clock. */
    cm_rtc_init();

    /* 7. Touch. */
    cm_touch_init();

    /* 8. Buttons. */
    cm_keys_init();

    /* 9. Storage card. */
    cm_sd_init();

    ESP_LOGI(TAG, "--- bring-up summary ---");
    ESP_LOGI(TAG, "  build : %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "  ioexp : %s", cm_board_ioexp_ok() ? "ok" : "FAIL");
    ESP_LOGI(TAG, "  tps   : %s (I2C standby ACK only, not rails)",
             cm_board_tps_ok() ? "ok" : "FAIL");
    ESP_LOGI(TAG, "  rails : %s (live PWR_GOOD)",
             cm_board_ioexp_get_level(EPD_TPS_IO_PWR_GOOD) ? "up" : "DOWN");
    ESP_LOGI(TAG, "  panel : %s", s_display_ready ? "ok" : "FAIL");
    ESP_LOGI(TAG, "  gauge : %s", cm_power_gauge_ready() ? "ok" : "FAIL");
    ESP_LOGI(TAG, "  chg   : %s", cm_power_charger_ready() ? "ok" : "FAIL");
    ESP_LOGI(TAG, "  touch : %s", cm_touch_ok() ? "ok" : "FAIL");
    ESP_LOGI(TAG, "  rtc   : %s", cm_rtc_ok() ? "ok" : "FAIL");
    ESP_LOGI(TAG, "  sd    : %s", cm_sd_ok() ? "ok" : "FAIL");

    int level = 0;
    bool charging = false, discharging = false;
    if (cm_power_battery_level(&level, &charging, &discharging)) {
        uint16_t mv = 0;
        cm_power_voltage_mv(&mv);
        ESP_LOGI(TAG, "  battery: %d%% %u mV %s", level, (unsigned)mv,
                 charging ? "charging" : (discharging ? "discharging" : "idle"));
    } else {
        ESP_LOGW(TAG, "  battery: unavailable");
    }

    ESP_LOGI(TAG, "bring-up complete");
}