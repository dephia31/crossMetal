/**
 * @file cm_i2c.c
 * @brief CrossMetal shared I2C bus implementation.
 *
 * Ported from Metalio-E-INK4-Plus `bus/metalio_i2c1.c` (MIT).
 * The bus-recovery bit-bang and the BQ27220 long SCL wait are preserved
 * deliberately: both were added upstream to fix real hardware faults.
 */

#include "cm_i2c.h"

#include "metalio_pinout.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "cm_i2c";

/* A half-powered slave can clamp SDA; upstream keeps a long wait for BQ27220. */
#define CM_I2C_SCL_WAIT_US 10000
#define CM_I2C_DEVICE_SLOTS 16

static StaticSemaphore_t s_mu_storage;
static SemaphoreHandle_t s_mu;
static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
static i2c_master_bus_handle_t s_bus;
static gpio_num_t s_sda, s_scl;

static struct {
    uint8_t addr;
    i2c_master_dev_handle_t handle;
} s_devices[CM_I2C_DEVICE_SLOTS];

static void ensure_mu(void) {
    if (__atomic_load_n(&s_mu, __ATOMIC_ACQUIRE) != NULL) {
        return;
    }
    portENTER_CRITICAL(&s_init_mux);
    if (s_mu == NULL) {
        /* Static creation: no heap allocation inside the interrupt-disabled lock. */
        __atomic_store_n(&s_mu, xSemaphoreCreateMutexStatic(&s_mu_storage),
                         __ATOMIC_RELEASE);
    }
    portEXIT_CRITICAL(&s_init_mux);
}

bool cm_i2c_lock(int timeout_ms) {
    ensure_mu();
    const TickType_t ticks =
        timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return s_mu != NULL && xSemaphoreTake(s_mu, ticks) == pdTRUE;
}

void cm_i2c_unlock(void) {
    if (s_mu != NULL) {
        xSemaphoreGive(s_mu);
    }
}

/*
 * Pull SDA/SCL back to open-drain GPIO and clock 9 pulses, then issue a manual
 * STOP. Needed when a slave browns out mid-transaction and holds SDA low, which
 * the peripheral's own bus-clear cannot always break.
 */
static void recover_i2c_bus_pins(gpio_num_t sda, gpio_num_t scl) {
    const gpio_config_t conf = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&conf);
    gpio_set_level(sda, 1);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(20);

    for (int i = 0; i < 9 && gpio_get_level(sda) == 0; ++i) {
        gpio_set_level(scl, 0);
        esp_rom_delay_us(5);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(5);
    }
    gpio_set_level(sda, 0);
    esp_rom_delay_us(5);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(5);
    gpio_set_level(sda, 1);
    esp_rom_delay_us(5);
}

esp_err_t cm_i2c_init(gpio_num_t sda, gpio_num_t scl) {
    if (!cm_i2c_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (s_bus != NULL) {
        err = (s_sda == sda && s_scl == scl) ? ESP_OK : ESP_ERR_INVALID_STATE;
    } else {
        recover_i2c_bus_pins(sda, scl);
        const i2c_master_bus_config_t config = {
            .i2c_port = CM_I2C_PORT,
            .sda_io_num = sda,
            .scl_io_num = scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags.enable_internal_pullup = true,
        };
        i2c_master_bus_handle_t created = NULL;
        err = i2c_new_master_bus(&config, &created);
        if (err == ESP_OK) {
            s_bus = created;
            s_sda = sda;
            s_scl = scl;
            ESP_LOGI(TAG, "I2C ready: SDA=%d SCL=%d %d Hz synchronous", sda, scl,
                     CM_I2C_SCL_HZ);
        } else {
            ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        }
    }
    cm_i2c_unlock();
    return err;
}

esp_err_t cm_i2c_deinit(void) {
    if (!cm_i2c_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (s_bus != NULL) {
        for (int i = 0; i < CM_I2C_DEVICE_SLOTS; ++i) {
            if (s_devices[i].handle == NULL) {
                continue;
            }
            err = i2c_master_bus_rm_device(s_devices[i].handle);
            if (err != ESP_OK) {
                break;
            }
            s_devices[i].handle = NULL;
        }
        if (err == ESP_OK) {
            err = i2c_del_master_bus(s_bus);
            if (err == ESP_OK) {
                s_bus = NULL;
            }
        }
    }
    cm_i2c_unlock();
    return err;
}

esp_err_t cm_i2c_device_locked(uint8_t addr, i2c_master_dev_handle_t *device) {
    if (device == NULL || addr > 0x7f) {
        return ESP_ERR_INVALID_ARG;
    }
    *device = NULL;
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int free_slot = -1;
    for (int i = 0; i < CM_I2C_DEVICE_SLOTS; ++i) {
        if (s_devices[i].handle != NULL && s_devices[i].addr == addr) {
            *device = s_devices[i].handle;
            return ESP_OK;
        }
        if (s_devices[i].handle == NULL && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return ESP_ERR_NO_MEM;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = CM_I2C_SCL_HZ,
        .scl_wait_us = CM_I2C_SCL_WAIT_US,
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &config, device);
    if (err == ESP_OK) {
        s_devices[free_slot].addr = addr;
        s_devices[free_slot].handle = *device;
    } else {
        ESP_LOGW(TAG, "add device 0x%02X: %s", addr, esp_err_to_name(err));
    }
    return err;
}

esp_err_t cm_i2c_probe_locked(uint8_t addr, int timeout_ms) {
    if (addr > 0x7f) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(s_bus, addr, timeout_ms);
}

esp_err_t cm_i2c_transmit_locked(uint8_t addr, const uint8_t *data, size_t len,
                                 int timeout_ms) {
    i2c_master_dev_handle_t device;
    esp_err_t err = cm_i2c_device_locked(addr, &device);
    return err == ESP_OK ? i2c_master_transmit(device, data, len, timeout_ms) : err;
}

esp_err_t cm_i2c_receive_locked(uint8_t addr, uint8_t *data, size_t len,
                                int timeout_ms) {
    i2c_master_dev_handle_t device;
    esp_err_t err = cm_i2c_device_locked(addr, &device);
    return err == ESP_OK ? i2c_master_receive(device, data, len, timeout_ms) : err;
}

esp_err_t cm_i2c_transmit_receive_locked(uint8_t addr, const uint8_t *tx,
                                         size_t tx_len, uint8_t *rx, size_t rx_len,
                                         int timeout_ms) {
    i2c_master_dev_handle_t device;
    esp_err_t err = cm_i2c_device_locked(addr, &device);
    return err == ESP_OK
               ? i2c_master_transmit_receive(device, tx, tx_len, rx, rx_len, timeout_ms)
               : err;
}

esp_err_t cm_i2c_write_reg_locked(uint8_t addr, uint8_t reg, const uint8_t *data,
                                  size_t len, int timeout_ms) {
    if (len != 0 && data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_dev_handle_t device;
    esp_err_t err = cm_i2c_device_locked(addr, &device);
    if (err != ESP_OK) {
        return err;
    }
    i2c_master_transmit_multi_buffer_info_t buffers[] = {
        {.write_buffer = &reg, .buffer_size = 1},
        {.write_buffer = data, .buffer_size = len},
    };
    return i2c_master_multi_buffer_transmit(device, buffers, len ? 2 : 1,
                                            timeout_ms);
}

esp_err_t cm_i2c_read_reg_stop_locked(uint8_t addr, uint8_t reg, uint8_t *data,
                                      size_t len, int timeout_ms) {
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = cm_i2c_transmit_locked(addr, &reg, 1, timeout_ms);
    if (err == ESP_OK) {
        err = cm_i2c_receive_locked(addr, data, len, timeout_ms);
    }
    return err;
}