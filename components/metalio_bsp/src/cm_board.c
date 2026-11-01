/**
 * @file cm_board.c
 * @brief CrossMetal board sequencing implementation.
 *
 * Ported from Metalio-E-INK4-Plus `display/epd_board_metalio_eink4_plus.c` (MIT).
 *
 * All TCA9555 writes go through a shadow copy of the output register rather than
 * a read-modify-write over I2C. The expander is configured as outputs on those
 * bits, so reading the input register would report the driven value anyway — but
 * a stale bus read during a chip hiccup would clobber unrelated bits on the same
 * port, and the rails and the motor share Port0.
 */

#include "cm_board.h"

#include "cm_i2c.h"
#include "cm_storage.h"
#include "cm_tca9555.h"
#include "cm_tps65185.h"
#include "metalio_pinout.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

static const char *TAG = "cm_board";

/* Shadow of what we last drove onto each port. */
static uint8_t s_p0_out;
static uint8_t s_p1_out;
static uint8_t s_motor_on;
static uint8_t s_pa_on;
static uint8_t s_cam_scr_on = METALIO_IO_BIT(EPD_IO_CAM_SCR_EN);
static bool s_bt_reset_pa_off;

static bool s_ioexp_ok;
static bool s_tps_ok;
static bool s_lcd_bus_held;

/* Port0 direction mask: these bits are outputs, everything else stays an input. */
#define CM_P0_OUT_MASK CM_BOARD_P0_OUT_MASK

/* Port1: the three gated/pulsed pins are outputs; keys and RTC INT are inputs. */
#define CM_P1_OUT_MASK CM_BOARD_P1_OUT_MASK

/* The rails that must survive a sleep cycle unchanged. */
#define CM_P1_RAIL_MASK                                                       \
    ((uint8_t)(METALIO_IO_BIT(EPD_IO_TP_RST) |                                \
               METALIO_IO_BIT(EPD_IO_BT_PA_PWR)))

/* Key pins driven high so the next scan reads "released". */
#define CM_P1_KEYS_IDLE_MASK                                                  \
    ((uint8_t)(METALIO_IO_BIT(EPD_IO_VOL_UP) |                                \
               METALIO_IO_BIT(EPD_IO_VOL_DOWN) |                              \
               METALIO_IO_BIT(EPD_IO_POWER)))

static SemaphoreHandle_t s_reset_mu;
static StaticSemaphore_t s_reset_storage;

static void ioexp_irq_setup(void);
static void ioexp_irq_isr(void *arg);
static SemaphoreHandle_t shared_reset_mutex(void);
static SemaphoreHandle_t s_ioexp_irq_sem;

static const gpio_num_t k_bus_pins[] = {
    EPD_PIN_D0,     EPD_PIN_D1,   EPD_PIN_D2, EPD_PIN_D3,  EPD_PIN_D4,
    EPD_PIN_D5,     EPD_PIN_D6,   EPD_PIN_D7, EPD_PIN_XCL, EPD_PIN_XLE,
    EPD_PIN_XOE,    EPD_PIN_XSTL, EPD_PIN_SPV, EPD_PIN_CKV, EPD_PIN_MODE,
    EPD_PIN_BORDER,
};

#define CM_BUS_PIN_COUNT (sizeof(k_bus_pins) / sizeof(k_bus_pins[0]))

/* --- output shadow helpers --------------------------------------------------- */

static uint8_t p0_output_value(void) {
    uint8_t value = 0;
    if (s_motor_on) {
        value |= (uint8_t)METALIO_IO_BIT(EPD_IO_MOTOR);
    }
    if (s_pa_on && !s_bt_reset_pa_off) {
        value |= (uint8_t)METALIO_IO_BIT(EPD_IO_PA_EN);
    }
    if (s_cam_scr_on) {
        value |= s_cam_scr_on;
    }
    /* TPS control lines are tracked in their own bits. */
    return (uint8_t)(value | (s_p0_out & (METALIO_IO_BIT(EPD_TPS_IO_VCOM_CTRL) |
                                          METALIO_IO_BIT(EPD_TPS_IO_PWRUP) |
                                          METALIO_IO_BIT(EPD_TPS_IO_WAKEUP))));
}

static void p0_write(void) {
    s_p0_out = p0_output_value();
    const esp_err_t err = cm_tca9555_set_value(s_p0_out, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "port0 write: %s", esp_err_to_name(err));
    }
}

static void p1_write(void) {
    const esp_err_t err = cm_tca9555_set_value(s_p1_out, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "port1 write: %s", esp_err_to_name(err));
    }
}

static void set_tps_bits(bool pvcom, bool pwrup, bool wakeup) {
    s_p0_out &= (uint8_t)~(METALIO_IO_BIT(EPD_TPS_IO_VCOM_CTRL) |
                           METALIO_IO_BIT(EPD_TPS_IO_PWRUP) |
                           METALIO_IO_BIT(EPD_TPS_IO_WAKEUP));
    if (pvcom) {
        s_p0_out |= (uint8_t)METALIO_IO_BIT(EPD_TPS_IO_VCOM_CTRL);
    }
    if (pwrup) {
        s_p0_out |= (uint8_t)METALIO_IO_BIT(EPD_TPS_IO_PWRUP);
    }
    if (wakeup) {
        s_p0_out |= (uint8_t)METALIO_IO_BIT(EPD_TPS_IO_WAKEUP);
    }
}

/* --- accessors --------------------------------------------------------------- */

bool cm_board_ioexp_ok(void) { return s_ioexp_ok; }
bool cm_board_tps_ok(void) { return s_tps_ok; }

esp_err_t cm_board_ioexp_write_port(int port, uint8_t value, uint8_t mask) {
    if (!s_ioexp_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mask == 0) {
        return ESP_OK;
    }

    /*
     * Merge into the authoritative image under the reset mutex, which already
     * guards every other expander state change on this board.
     */
    SemaphoreHandle_t mu = shared_reset_mutex();
    xSemaphoreTake(mu, portMAX_DELAY);

    esp_err_t err;
    if (port == 0) {
        s_p0_out = (uint8_t)((s_p0_out & ~mask) | (value & mask));
        err = cm_tca9555_set_value(s_p0_out, 0);
    } else if (port == 1) {
        s_p1_out = (uint8_t)((s_p1_out & ~mask) | (value & mask));
        err = cm_tca9555_set_value(s_p1_out, 1);
    } else {
        err = ESP_ERR_INVALID_ARG;
    }

    xSemaphoreGive(mu);
    return err;
}

void cm_board_ioexp_irq_reclaim(void) {
    if (!s_ioexp_ok) {
        return;
    }
    /*
     * The vendored display glue installs its own GPIO2 handler during
     * `epd_init()`, which replaces ours (the ISR service keeps one handler per
     * pin). Without this, key scanning loses its interrupt and degrades to the
     * 50 ms poll.
     */
    gpio_isr_handler_remove(METALIO_IO_EXPANDER_INT_GPIO);
    const esp_err_t err =
        gpio_isr_handler_add(METALIO_IO_EXPANDER_INT_GPIO, ioexp_irq_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot reclaim expander IRQ on GPIO%d: %s",
                 METALIO_IO_EXPANDER_INT_GPIO, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "expander IRQ reclaimed on GPIO%d", METALIO_IO_EXPANDER_INT_GPIO);
}

/* --- init -------------------------------------------------------------------- */

esp_err_t cm_board_init(void) {
    /* gpio_hold latches outlive a reset: clear them before touching anything. */
    cm_board_boot_release_holds();

    esp_err_t err = cm_i2c_init(METALIO_I2C_SDA_PIN, METALIO_I2C_SCL_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!cm_i2c_lock(100)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t probe = cm_i2c_probe_locked(METALIO_TCA9555_I2C_ADDR, 50);
    cm_i2c_unlock();

    if (probe != ESP_OK) {
        ESP_LOGE(TAG, "TCA9555 not present @0x%02X", METALIO_TCA9555_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    /* P1.2 (touch reset) and P1.4 (BT/PA rail) idle HIGH, as upstream. */
    s_p1_out = (uint8_t)(METALIO_IO_BIT(EPD_IO_TP_RST) |
                         METALIO_IO_BIT(EPD_IO_BT_PA_PWR));
    /* Camera/screen rail starts enabled. */
    s_cam_scr_on = (uint8_t)METALIO_IO_BIT(EPD_IO_CAM_SCR_EN);
    s_p0_out = p0_output_value();

    /* Drive outputs first, then set directions: avoids a glitch on the rails. */
    cm_tca9555_set_value(s_p0_out, 0);
    cm_tca9555_set_value(s_p1_out, 1);

    /*
     * Port0: TPS control + motor + PA + camera rail are outputs; PWR_GOOD and
     * the rest stay inputs.
     */
    cm_tca9555_set_config((uint8_t)~CM_P0_OUT_MASK, 0);

    /*
     * Port1: P1.2 / P1.4 / P1.3 are outputs; P1.0, P1.1 (volume) and P1.6
     * (RTC INT) stay inputs.
     */
    cm_tca9555_set_config((uint8_t)~CM_P1_OUT_MASK, 1);

    s_ioexp_ok = true;

    /*
     * The TPS does not answer I2C until WAKEUP has been high for 50 ms: it
     * powers up in SLEEP. Driving WAKEUP first is also what upstream does, so
     * the probe sees the same state a vendor boot does.
     */
    set_tps_bits(false, false, true);
    p0_write();
    vTaskDelay(pdMS_TO_TICKS(50));

    err = cm_tps_probe();
    if (err == ESP_OK) {
        s_tps_ok = true;
        /* Keep the panel biased at the value upstream ships with. */
        cm_tps_set_vcom(METALIO_EPD_VCOM_MV);
        ESP_LOGI(TAG, "TPS65185 ready @0x%02X, VCOM=%d mV", CM_TPS65185_I2C_ADDR,
                 METALIO_EPD_VCOM_MV);
    } else {
        ESP_LOGW(TAG, "TPS65185 not present @0x%02X: %s", CM_TPS65185_I2C_ADDR,
                 esp_err_to_name(err));
    }

    ioexp_irq_setup();

    ESP_LOGI(TAG, "board init done: ioexp=%s tps=%s", s_ioexp_ok ? "ok" : "FAIL",
             s_tps_ok ? "ok" : "FAIL");
    return ESP_OK;
}

/* --- display power ----------------------------------------------------------- */

esp_err_t cm_board_display_power_on(void) {
    if (!s_ioexp_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    /*
     * Already in STANDBY; `epd_board_poweron_once()` owns the vendor up-sequence.
     * This reports whether the PMIC is present.
     */
    return s_tps_ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/*
 * Rail teardown is deliberately not implemented in this layer.
 *
 * The vendored display driver owns the panel power state: `epd_board_poweron_once()`
 * raises the rails and `epd_board_poweroff()` lowers them in the datasheet order
 * (XOE low, 12 us, then PWRUP/VCOM low, then WAKEUP low). A second teardown here
 * would write the same expander bits from a shadow the driver never sees: its
 * `config_reg` is not updated by board-layer writes, so the next frame's
 * `epd_board_set_ctrl()` would re-energise the panel from its stale copy.
 *
 * Cut the panel through `cm_display_power_off()` instead.
 */

/* --- shared reset ------------------------------------------------------------ */

static SemaphoreHandle_t shared_reset_mutex(void) {
    if (s_reset_mu == NULL) {
        s_reset_mu = xSemaphoreCreateMutexStatic(&s_reset_storage);
    }
    return s_reset_mu;
}

esp_err_t cm_board_shared_reset_acquire(uint32_t timeout_ms) {
    SemaphoreHandle_t mu = shared_reset_mutex();
    if (xSemaphoreTake(mu, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "shared P1.2 reset lock timeout (%u ms)", (unsigned)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void cm_board_shared_reset_release(void) {
    SemaphoreHandle_t mu = shared_reset_mutex();
    xSemaphoreGive(mu);
}

void cm_board_touch_reset(void) {
    if (!s_ioexp_ok) {
        ESP_LOGW(TAG, "touch reset skipped: no expander");
        vTaskDelay(pdMS_TO_TICKS(300));
        return;
    }

    /* P1.2 is shared with the camera and the 4G module: serialise access. */
    SemaphoreHandle_t mu = shared_reset_mutex();
    xSemaphoreTake(mu, portMAX_DELAY);

    s_p1_out &= (uint8_t)~METALIO_IO_BIT(EPD_IO_TP_RST);
    p1_write();
    vTaskDelay(pdMS_TO_TICKS(10)); /* Trst >= 5 ms */

    s_p1_out |= (uint8_t)METALIO_IO_BIT(EPD_IO_TP_RST);
    p1_write();
    vTaskDelay(pdMS_TO_TICKS(300)); /* Trsi >= 300 ms */

    xSemaphoreGive(mu);
    ESP_LOGI(TAG, "touch reset pulsed on TCA P1.2");
}

/* --- misc peripherals -------------------------------------------------------- */

void cm_board_motor_set(bool on) {
    if (!s_ioexp_ok) {
        return;
    }
    s_motor_on = on ? (uint8_t)METALIO_IO_BIT(EPD_IO_MOTOR) : 0;
    p0_write();
}

void cm_board_pa_set(bool on) {
    if (!s_ioexp_ok) {
        return;
    }
    s_pa_on = on ? (uint8_t)METALIO_IO_BIT(EPD_IO_PA_EN) : 0;
    p0_write();
}

void cm_board_cam_scr_set(bool on) {
    if (!s_ioexp_ok) {
        return;
    }
    s_cam_scr_on = on ? (uint8_t)METALIO_IO_BIT(EPD_IO_CAM_SCR_EN) : 0;
    p0_write();
}

void cm_board_ana_sw_set(bool high) {
    static bool gpio_ready;
    if (!gpio_ready) {
        gpio_reset_pin(METALIO_ANA_SW_GPIO);
        gpio_set_direction(METALIO_ANA_SW_GPIO, GPIO_MODE_OUTPUT);
        gpio_ready = true;
    }
    gpio_set_level(METALIO_ANA_SW_GPIO, high ? 1 : 0);
}

void cm_board_pwr_key_pulse_train(void) {
    if (!s_ioexp_ok) {
        ESP_LOGW(TAG, "power-off pulse skipped: no expander");
        return;
    }
    ESP_LOGI(TAG, "power-off pulse train x%d, %d ms each half",
             METALIO_PWR_PULSE_COUNT, METALIO_PWR_PULSE_INTERVAL_MS);

    for (int i = 0; i < METALIO_PWR_PULSE_COUNT; ++i) {
        s_p1_out |= (uint8_t)METALIO_IO_BIT(EPD_IO_PWR_PULSE);
        p1_write();
        vTaskDelay(pdMS_TO_TICKS(METALIO_PWR_PULSE_INTERVAL_MS));

        s_p1_out &= (uint8_t)~METALIO_IO_BIT(EPD_IO_PWR_PULSE);
        p1_write();
        vTaskDelay(pdMS_TO_TICKS(METALIO_PWR_PULSE_INTERVAL_MS));
    }
}

bool cm_board_ioexp_get_level(int io) {
    /* Remember the last good value: a failed read must not invent a key press. */
    static uint8_t s_last[2] = {0xFF, 0xFF};
    if (!s_ioexp_ok || io < 0 || io > 15) {
        return true;
    }
    const int port = METALIO_IO_PORT(io);
    uint8_t v = s_last[port];
    if (cm_tca9555_read_input(port, &v) == ESP_OK) {
        s_last[port] = v;
    }
    return (s_last[port] & METALIO_IO_BIT(io)) != 0;
}

bool cm_board_ioexp_read_port(int port, uint8_t *out) {
    if (!s_ioexp_ok || out == NULL || port < 0 || port > 1) {
        return false;
    }
    return cm_tca9555_read_input(port, out) == ESP_OK;
}

/*
 * GPIO2 carries the expander's interrupt, wired-OR with the accelerometer's.
 * It is open-drain and active low, so a falling edge is the signal.
 */
static void IRAM_ATTR ioexp_irq_isr(void *arg) {
    (void)arg;
    BaseType_t hp = pdFALSE;
    if (s_ioexp_irq_sem != NULL) {
        xSemaphoreGiveFromISR(s_ioexp_irq_sem, &hp);
    }
    if (hp == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void ioexp_irq_setup(void) {
    if (s_ioexp_irq_sem == NULL) {
        s_ioexp_irq_sem = xSemaphoreCreateBinary();
        if (s_ioexp_irq_sem == NULL) {
            return;
        }
    }

    const gpio_config_t conf = {
        .pin_bit_mask = 1ULL << (unsigned)METALIO_IO_EXPANDER_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&conf);

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "isr service: %s", esp_err_to_name(err));
    }
    gpio_isr_handler_remove(METALIO_IO_EXPANDER_INT_GPIO);
    if (gpio_isr_handler_add(METALIO_IO_EXPANDER_INT_GPIO, ioexp_irq_isr, NULL) !=
        ESP_OK) {
        ESP_LOGE(TAG, "cannot attach expander IRQ on GPIO%d",
                 METALIO_IO_EXPANDER_INT_GPIO);
    }
}

bool cm_board_ioexp_irq_wait(uint32_t timeout_ms) {
    if (s_ioexp_irq_sem == NULL) {
        vTaskDelay(pdMS_TO_TICKS(timeout_ms == 0 ? 1 : timeout_ms));
        return false;
    }
    return xSemaphoreTake(s_ioexp_irq_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

/* --- rails ------------------------------------------------------------------- */

void cm_board_sleep_cut_rails(void) {
    if (!s_ioexp_ok) {
        return;
    }
    s_cam_scr_on = 0;
    p0_write();
    s_p1_out &= (uint8_t)~METALIO_IO_BIT(EPD_IO_BT_PA_PWR);
    p1_write();
    ESP_LOGI(TAG, "sleep: cut P0.5 then P1.4");
}

void cm_board_sleep_restore_rails(void) {
    if (!s_ioexp_ok) {
        return;
    }
    s_p1_out |= (uint8_t)METALIO_IO_BIT(EPD_IO_BT_PA_PWR);
    p1_write();
    s_cam_scr_on = (uint8_t)METALIO_IO_BIT(EPD_IO_CAM_SCR_EN);
    p0_write();
    ESP_LOGI(TAG, "sleep: restore P1.4 then P0.5");
}

/* --- expander sleep masking -------------------------------------------------- */

void cm_board_ioexp_mask_for_sleep(void) {
    if (!s_ioexp_ok) {
        return;
    }

    /*
     * Port0: drop TPS lines, keep motor/PA/camera latched, make all outputs to mask
     * the interrupt. Caller must have called `cm_display_power_off()` first so XOE
     * is low when rails fall.
     */
    set_tps_bits(false, false, false);
    p0_write();
    cm_tca9555_set_config(0x00, 0);

    /*
     * Port1: preserve gated rails, force key pins idle-high (driving them low then
     * switching to input on wake would read as press), clear shutdown pulse.
     */
    s_p1_out &= (uint8_t)CM_P1_RAIL_MASK;
    s_p1_out |= (uint8_t)CM_P1_KEYS_IDLE_MASK;
    s_p1_out &= (uint8_t)~METALIO_IO_BIT(EPD_IO_PWR_PULSE);
    p1_write();
    cm_tca9555_set_config(0x00, 1);

    uint8_t p0 = 0, p1 = 0;
    cm_tca9555_read_input(0, &p0);
    cm_tca9555_read_input(1, &p1);
    ESP_LOGI(TAG, "sleep mask: all-out, TPS low, keys idle-high (P0=0x%02X P1=0x%02X)",
             p0, p1);
}

bool cm_board_ioexp_enable_power_wake_pin(void) {
    if (!s_ioexp_ok) {
        return false;
    }
    /*
     * Re-arm P1.7 as an input. Its latched output level is already high, so the
     * transition should not generate a false edge.
     */
    if (cm_tca9555_set_config((uint8_t)METALIO_IO_BIT(EPD_IO_POWER), 1) != ESP_OK) {
        ESP_LOGW(TAG, "cannot re-arm power wake pin P1.7");
        return false;
    }
    return true;
}

void cm_board_ioexp_restore_after_sleep(void) {
    if (!s_ioexp_ok) {
        return;
    }

    /* Port0 back to its normal output set. */
    p0_write();
    cm_tca9555_set_config((uint8_t)~CM_P0_OUT_MASK, 0);

    /* Port1: keep the rails, clear the pulse, send the keys back to inputs. */
    s_p1_out &= (uint8_t)CM_P1_RAIL_MASK;
    s_p1_out &= (uint8_t)~METALIO_IO_BIT(EPD_IO_PWR_PULSE);
    p1_write();
    cm_tca9555_set_config((uint8_t)~CM_P1_OUT_MASK, 1);

    uint8_t p0 = 0, p1 = 0;
    cm_tca9555_read_input(0, &p0);
    cm_tca9555_read_input(1, &p1);
    ESP_LOGI(TAG, "wake: ports restored (P0=0x%02X P1=0x%02X)", p0, p1);
}

/* --- bus hold ---------------------------------------------------------------- */

void cm_board_bus_hold_low(void) {
    for (size_t i = 0; i < CM_BUS_PIN_COUNT; ++i) {
        gpio_hold_dis(k_bus_pins[i]);
    }
    s_lcd_bus_held = true;
    for (size_t i = 0; i < CM_BUS_PIN_COUNT; ++i) {
        const gpio_num_t pin = k_bus_pins[i];
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_pull_mode(pin, GPIO_FLOATING);
        gpio_set_level(pin, 0);
    }
    ESP_LOGI(TAG, "panel bus driven low");
}

void cm_board_bus_latch_for_sleep(void) {
    for (size_t i = 0; i < CM_BUS_PIN_COUNT; ++i) {
        const esp_err_t err = gpio_hold_en(k_bus_pins[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "gpio_hold_en(%d): %s", (int)k_bus_pins[i],
                     esp_err_to_name(err));
        }
    }
}

void cm_board_bus_restore(void) {
    if (!s_lcd_bus_held) {
        return;
    }
    for (size_t i = 0; i < CM_BUS_PIN_COUNT; ++i) {
        gpio_hold_dis(k_bus_pins[i]);
    }
    s_lcd_bus_held = false;
    ESP_LOGI(TAG, "panel bus released");
}

void cm_board_boot_release_holds(void) {
    /*
     * A deep-sleep gpio_hold survives the CPU reset that ends it. Without this
     * the panel bus, the SD pins and the frontlight stay latched low, and the
     * PMIC's boost then fails to start.
     */
    for (size_t i = 0; i < CM_BUS_PIN_COUNT; ++i) {
        gpio_hold_dis(k_bus_pins[i]);
    }
    cm_sd_release_hold();
    gpio_hold_dis(METALIO_I2C_SDA_PIN);
    gpio_hold_dis(METALIO_I2C_SCL_PIN);
    gpio_hold_dis(METALIO_FL_COOL_PIN);
    gpio_hold_dis(METALIO_FL_WARM_PIN);
    s_lcd_bus_held = false;
    ESP_LOGI(TAG, "boot: released gpio holds");
}