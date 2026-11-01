/**
 * @file cm_display.c
 * @brief CrossMetal display facade implementation.
 *
 * Mirrors upstream `display/metalio_epd.c` (MIT). The vendored EPDiy stack does
 * the work; this file owns ordering and the public surface.
 *
 * Two upstream behaviours are preserved deliberately:
 *
 *  1. The panel is left in native landscape (`EPD_ROT_LANDSCAPE`). Upstream's
 *     comment is explicit that `EPD_ROT_INVERTED_PORTRAIT` must not be used,
 *     because the UI applies the portrait transform itself.
 *  2. The verbose per-frame logging inside EPDiy is muted unless
 *     `CM_EPD_VERBOSE_LOG` is defined. A refresh path that logs per line is
 *     unusable on a live reader.
 */

#include "cm_display.h"

#include "cm_board.h"
#include "metalio_pinout.h"

#include "cm_tps65185.h"
#include "config.h"
#include "epd_board_metalio_eink4_plus.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "metalio_pinout.h"
#include "my_waveform.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

static const char *TAG = "cm_display";

#ifndef CM_EPD_VERBOSE_LOG
#define CM_EPD_VERBOSE_LOG 0
#endif

static EpdiyHighlevelState s_hl;
static bool s_ready;

/*
 * Map the facade's small mode set onto EPDiy's flags.
 *
 * The highlevel renderer needs a packing mode to know the framebuffer layout,
 * and a "previously" hint so it can pick an optimised transition. The UI always
 * hands us a full 4-bpp image, so `MODE_PACKING_2PPB` is constant here.
 */
static enum EpdDrawMode to_epd_mode(cm_epd_mode_t mode) {
    switch (mode) {
        case CM_EPD_MODE_GC16:
            return (enum EpdDrawMode)(MODE_GC16 | MODE_PACKING_2PPB);
        case CM_EPD_MODE_DU:
            return (enum EpdDrawMode)(MODE_DU | MODE_PACKING_2PPB);
        case CM_EPD_MODE_GL16:
        default:
            return (enum EpdDrawMode)(EPD_MODE_DEFAULT | MODE_PACKING_2PPB);
    }
}

bool cm_display_init(void) {
    if (s_ready) {
        return true;
    }

#if !CM_EPD_VERBOSE_LOG
    /* The refresh path is chatty; keep it quiet unless explicitly asked. */
    esp_log_level_set("epdiy", ESP_LOG_WARN);
    esp_log_level_set("epd_path", ESP_LOG_WARN);
    esp_log_level_set("epd_lcd", ESP_LOG_WARN);
    esp_log_level_set("epd_eink4p", ESP_LOG_WARN);
    esp_log_level_set("epd", ESP_LOG_WARN);
#endif

    ESP_LOGI(TAG, "EPDiy init: E0470A14-AF 34-pin, VCOM=%d mV", EPD_VCOM_MV);

    const size_t free_internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t free_dma_before =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    epd_init(&epd_board_metalio_eink4_plus, &E0470A14_AF, EPD_LUT_1K);
    epd_set_vcom(EPD_VCOM_MV);

    /* Native landscape, matching the UI's own portrait transform. */
    epd_set_rotation(EPD_ROT_LANDSCAPE);

    my_waveform_init();
    s_hl = epd_hl_init(&MY_WAVEFORM);

    const size_t free_internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t free_dma_after =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    const size_t used_internal =
        free_internal_before > free_internal_after
            ? free_internal_before - free_internal_after
            : 0;
    const size_t used_dma =
        free_dma_before > free_dma_after ? free_dma_before - free_dma_after : 0;

    ESP_LOGI(TAG, "EPDiy DRAM: internal used %u B, DMA-capable used %u B",
             (unsigned)used_internal, (unsigned)used_dma);
    ESP_LOGI(TAG, "free internal %u -> %u KB",
             (unsigned)(free_internal_before / 1024), (unsigned)(free_internal_after / 1024));

    if (s_hl.front_fb == NULL) {
        ESP_LOGE(TAG, "framebuffer allocation failed (front_fb is NULL)");
        return false;
    }

    /*
     * Power on the panel and leave it powered: every draw needs the rails up.
     * `epd_poweron()` runs the board's rail sequence, setting the TPS-ready flag.
     */
    epd_poweron();
    s_ready = epd_board_metalio_eink4_plus_tps_ok();
    if (!s_ready) {
        ESP_LOGE(TAG, "TPS65185 never reported ready; panel cannot be driven");
        cm_tps_debug_error();
        return false;
    }
    /* Drop output-enable (XOE) so source drivers idle until the first frame. */
    epd_set_mode(0);

    ESP_LOGI(TAG, "ready: native %dx%d (UI logical %dx%d)", epd_width(), epd_height(),
             METALIO_DISPLAY_WIDTH, METALIO_DISPLAY_HEIGHT);
    return true;
}

bool cm_display_ready(void) { return s_ready; }

int cm_display_width(void) { return epd_width(); }
int cm_display_height(void) { return epd_height(); }

uint8_t *cm_display_framebuffer(void) {
    return s_ready ? s_hl.front_fb : NULL;
}

void cm_display_clear_buffer(void) {
    if (!s_ready) {
        return;
    }
    epd_hl_set_all_white(&s_hl);
}

esp_err_t cm_display_flush_full(cm_epd_mode_t mode, int temperature) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    /*
     * Warn if PWR_GOOD is low. Best effort: the expander read may fail, returning
     * the last good value, so this warns rather than gating the draw.
     */
    if (!cm_board_ioexp_get_level(EPD_TPS_IO_PWR_GOOD)) {
        ESP_LOGE(TAG, "flush while PWR_GOOD is low; the frame will not appear");
    }
    const enum EpdDrawError err =
        epd_hl_update_screen(&s_hl, to_epd_mode(mode), temperature);
    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGE(TAG, "flush failed, error flags 0x%X", (unsigned)err);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t cm_display_flush(cm_epd_mode_t mode, int temperature) {
    return cm_display_flush_full(mode, temperature);
}

esp_err_t cm_display_clear_screen(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Slow, visibly blinks the panel; reserved for recovery and conditioning. */
    const enum EpdDrawError err = epd_clear_checked();
    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGE(TAG, "clear failed, error flags 0x%X", (unsigned)err);
        return ESP_FAIL;
    }
    epd_hl_set_all_white(&s_hl);
    return ESP_OK;
}

void cm_display_power_off(void) {
    if (s_ready) {
        epd_poweroff();
    }
}

void cm_display_set_vcom(uint16_t vcom_mv) {
    epd_set_vcom(vcom_mv);
}