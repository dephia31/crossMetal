/**
 * @file cm_storage.c
 * @brief CrossMetal microSD implementation.
 *
 * Ported from Metalio-E-INK4-Plus `periph/metalio_sd.cc` (MIT).
 *
 * Upstream's sd_pin_force_low() and the CNNT pad release are carried over
 * verbatim in behaviour. The CNNT block is only present on ESP32-S31, so that
 * part is compiled behind CONFIG_IDF_TARGET_ESP32S31.
 */

#include "cm_storage.h"

#include "metalio_pinout.h"

#include <driver/gpio.h>
#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <esp_private/esp_gpio_reserve.h>
#include <esp_rom_gpio.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <soc/gpio_sig_map.h>

#if CONFIG_IDF_TARGET_ESP32S31
#include <soc/cnnt_io_mux_struct.h>
#endif

static const char *TAG = "cm_storage";

static sdmmc_card_t *s_card;
static bool s_mounted;

static const gpio_num_t k_sd_pins[] = {
    METALIO_SDMMC_CLK_PIN, METALIO_SDMMC_CMD_PIN,  METALIO_SDMMC_D0_PIN,
    METALIO_SDMMC_D1_PIN,  METALIO_SDMMC_D2_PIN,   METALIO_SDMMC_D3_PIN,
};

#define CM_SD_PIN_COUNT (sizeof(k_sd_pins) / sizeof(k_sd_pins[0]))

#if CONFIG_IDF_TARGET_ESP32S31
/*
 * On S31 the SDIO pads default to the CNNT peripheral's alternate function, not
 * GPIO. Upstream releases them by clearing the dedicated-function select and the
 * pad pull controls so the pads can be driven from the GPIO matrix.
 */
static void sdio_cnnt_release_to_gpio(void) {
    CNNT_PAD_CTRL.sdio_clk.sdio_clk_fun_wpu = 0;
    CNNT_PAD_CTRL.sdio_clk.sdio_clk_fun_wpd = 1;
    CNNT_PAD_CTRL.sdio_cmd.sdio_cmd_fun_wpu = 0;
    CNNT_PAD_CTRL.sdio_cmd.sdio_cmd_fun_wpd = 1;
    CNNT_PAD_CTRL.sdio_data0.sdio_data0_fun_wpu = 0;
    CNNT_PAD_CTRL.sdio_data0.sdio_data0_fun_wpd = 1;
    CNNT_PAD_CTRL.sdio_data1.sdio_data1_fun_wpu = 0;
    CNNT_PAD_CTRL.sdio_data1.sdio_data1_fun_wpd = 1;
    CNNT_PAD_CTRL.sdio_data2.sdio_data2_fun_wpu = 0;
    CNNT_PAD_CTRL.sdio_data2.sdio_data2_fun_wpd = 1;
    CNNT_PAD_CTRL.sdio_data3.sdio_data3_fun_wpu = 0;
    CNNT_PAD_CTRL.sdio_data3.sdio_data3_fun_wpd = 1;
    CNNT_PAD_CTRL.ctrl.sdio_pad_pin_ctrl_ded_sel = 0;
    ESP_LOGI(TAG, "CNNT SDIO pads released to GPIO");
}
#endif

/* Force one pin to a driven low output, taking it off any peripheral. */
static void sd_pin_force_low(gpio_num_t pin) {
    gpio_hold_dis(pin);
    esp_gpio_revoke(1ULL << (unsigned)pin);
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    const gpio_config_t conf = {
        .pin_bit_mask = 1ULL << (unsigned)pin,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&conf);
    gpio_pullup_dis(pin);
    gpio_pulldown_dis(pin);
    gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_3);
    gpio_set_level(pin, 0);
}

void cm_sd_release_hold(void) {
    for (size_t i = 0; i < CM_SD_PIN_COUNT; ++i) {
        gpio_hold_dis(k_sd_pins[i]);
    }
}

bool cm_sd_init(void) {
    if (s_mounted) {
        return true;
    }
    cm_sd_release_hold();

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    /* 4-bit bus, as wired. */
    host.flags = SDMMC_HOST_FLAG_4BIT;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = METALIO_SDMMC_CLK_PIN;
    slot.cmd = METALIO_SDMMC_CMD_PIN;
    slot.d0 = METALIO_SDMMC_D0_PIN;
    slot.d1 = METALIO_SDMMC_D1_PIN;
    slot.d2 = METALIO_SDMMC_D2_PIN;
    slot.d3 = METALIO_SDMMC_D3_PIN;
    /* The board has external pull-ups on the SD lines. */
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_err_t err = esp_vfs_fat_sdmmc_mount(CM_SD_MOUNT_POINT, &host, &slot,
                                                  &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return false;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "mounted at %s", CM_SD_MOUNT_POINT);
    return true;
}

bool cm_sd_ok(void) { return s_mounted; }

bool cm_sd_unmount(void) {
    if (!s_mounted) {
        return true;
    }
    const esp_err_t err = esp_vfs_fat_sdcard_unmount(CM_SD_MOUNT_POINT, s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "unmount: %s", esp_err_to_name(err));
        return false;
    }
    s_card = NULL;
    s_mounted = false;
    return true;
}

bool cm_sd_hold_pins_low(void) {
    cm_sd_release_hold();
    if (!cm_sd_unmount()) {
        return false;
    }
#if CONFIG_IDF_TARGET_ESP32S31
    sdio_cnnt_release_to_gpio();
#endif
    for (size_t i = 0; i < CM_SD_PIN_COUNT; ++i) {
        sd_pin_force_low(k_sd_pins[i]);
    }
    ESP_LOGI(TAG, "SD pins parked low");
    return true;
}

void cm_sd_latch_for_sleep(void) {
    for (size_t i = 0; i < CM_SD_PIN_COUNT; ++i) {
        gpio_set_level(k_sd_pins[i], 0);
        const esp_err_t err = gpio_hold_en(k_sd_pins[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "gpio_hold_en(%d): %s", (int)k_sd_pins[i],
                     esp_err_to_name(err));
        }
    }
}