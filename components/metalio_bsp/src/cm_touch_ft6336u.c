/**
 * @file cm_touch.c
 * @brief CrossMetal FT6336U implementation.
 *
 * Ported from Metalio-E-INK4-Plus `display/metalio_touch.c` (MIT).
 *
 * The INT line is treated as a latency hint, not as the source of truth: the
 * task also polls on a timeout, so a missed edge degrades latency instead of
 * disabling input. Upstream learned this the hard way and it is preserved.
 */

#include "cm_touch.h"

#include "cm_board.h"
#include "cm_i2c.h"
#include "metalio_pinout.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

static const char *TAG = "cm_touch";

#define FT_REG_TD_STATUS 0x02
#define FT_REG_CHIP_ID 0xA3
#define FT_REG_FW_VER 0xA6
#define FT_REG_INT_MODE 0xA4
#define FT_CHIP_ID_6336 0x64

#define FT_EVENT_DOWN 0
#define FT_EVENT_CONTACT 2

#define TOUCH_LOCK_MS 50
#define TOUCH_FEED_MS 10
#define TOUCH_FEED_PRIO 5
#define TOUCH_FEED_STACK 4096

/* Panel is mounted rotated: the controller's Y axis is the UI's X axis. */
#define LOGICAL_W METALIO_DISPLAY_WIDTH  /* 684  */
#define LOGICAL_H METALIO_DISPLAY_HEIGHT /* 1216 */

static bool s_ok;
static bool s_finger_down;
static volatile int s_feed_pause_depth;

static SemaphoreHandle_t s_irq_sem;
static TaskHandle_t s_feed_task;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static cm_touch_point_t s_ui_pt;
static bool s_ui_pressed;
static bool s_ui_seen;
static bool s_replay_press;
static bool s_replay_release;

static cm_touch_cb_t s_cb;
static uint32_t s_samples;
static uint32_t s_read_errors;
static int s_last_raw_x;
static int s_last_raw_y;

static esp_err_t ft_read(uint8_t reg, uint8_t *buf, size_t n) {
    if (n == 0) {
        return ESP_OK;
    }
    if (!cm_i2c_lock(TOUCH_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    /* A pause can begin while this task waits for the bus; recheck under the
     * lock so a read cannot start after a flash drain has begun. */
    if (s_feed_pause_depth > 0) {
        cm_i2c_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t ret = cm_i2c_transmit_receive_locked(
        METALIO_TOUCH_I2C_ADDR, &reg, 1, buf, n, 50);
    cm_i2c_unlock();
    return ret;
}

static esp_err_t ft_write(uint8_t reg, uint8_t val) {
    if (!cm_i2c_lock(TOUCH_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t ret =
        cm_i2c_write_reg_locked(METALIO_TOUCH_I2C_ADDR, reg, &val, 1, 50);
    cm_i2c_unlock();
    return ret;
}

/*
 * Controller landscape frame -> UI portrait frame.
 *
 * A point is already in UI coordinates when its Y fits inside the panel width
 * and its X does too. Otherwise it is a rotated sample and needs the transpose.
 */
static void map_to_logical(int raw_x, int raw_y, int *lx, int *ly) {
    int x = raw_x;
    int y = raw_y;

    if (raw_y >= LOGICAL_W && raw_x < LOGICAL_W) {
        x = raw_x;
        y = raw_y;
    } else if (raw_x >= LOGICAL_W) {
        x = LOGICAL_W - 1 - raw_y;
        y = raw_x;
    }

    if (x < 0) {
        x = 0;
    } else if (x >= LOGICAL_W) {
        x = LOGICAL_W - 1;
    }
    if (y < 0) {
        y = 0;
    } else if (y >= LOGICAL_H) {
        y = LOGICAL_H - 1;
    }

    *lx = x;
    *ly = y;
}

cm_vk_t cm_touch_classify_vk(int raw_x, int raw_y) {
    /* Keys live in a band just outside the panel's Y extent. */
    if (raw_y < (METALIO_TOUCH_VK_Y - METALIO_TOUCH_VK_TOL) ||
        raw_y > (METALIO_TOUCH_VK_Y + METALIO_TOUCH_VK_TOL)) {
        return CM_VK_NONE;
    }
    if (raw_x >= (METALIO_TOUCH_VK_HOME_X - METALIO_TOUCH_VK_TOL) &&
        raw_x <= (METALIO_TOUCH_VK_HOME_X + METALIO_TOUCH_VK_TOL)) {
        return CM_VK_HOME;
    }
    if (raw_x >= (METALIO_TOUCH_VK_PREV_X - METALIO_TOUCH_VK_TOL) &&
        raw_x <= (METALIO_TOUCH_VK_PREV_X + METALIO_TOUCH_VK_TOL)) {
        return CM_VK_PREV;
    }
    if (raw_x >= (METALIO_TOUCH_VK_NEXT_X - METALIO_TOUCH_VK_TOL) &&
        raw_x <= (METALIO_TOUCH_VK_NEXT_X + METALIO_TOUCH_VK_TOL)) {
        return CM_VK_NEXT;
    }
    return CM_VK_NONE;
}

static void IRAM_ATTR touch_isr(void *arg) {
    (void)arg;
    if (s_feed_pause_depth > 0) {
        return;
    }
    BaseType_t hp = pdFALSE;
    if (s_irq_sem != NULL) {
        xSemaphoreGiveFromISR(s_irq_sem, &hp);
    }
    if (hp == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void publish_ui(bool pressed, int lx, int ly, int raw_x, int raw_y) {
    portENTER_CRITICAL(&s_mux);
    if (pressed) {
        s_ui_pt.pressed = true;
        s_ui_pt.x = lx;
        s_ui_pt.y = ly;
        s_ui_pt.raw_x = raw_x;
        s_ui_pt.raw_y = raw_y;
        if (!s_ui_pressed) {
            s_ui_seen = false;
            s_replay_press = false;
            s_replay_release = false;
        }
        s_ui_pressed = true;
    } else {
        /* If the UI never got to read the press, queue a press+release pair so a
         * tap that landed entirely inside a blocking refresh is not lost. */
        if (s_ui_pressed && !s_ui_seen) {
            s_replay_press = true;
            s_replay_release = true;
        }
        s_ui_pressed = false;
        s_ui_seen = false;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void feed_one_sample(void) {
    uint8_t buf[6] = {0};
    const esp_err_t result = ft_read(FT_REG_TD_STATUS, buf, sizeof(buf));
    if (result == ESP_ERR_INVALID_STATE) {
        return; /* paused on purpose, not a chip error */
    }
    ++s_samples;
    if (result != ESP_OK) {
        ++s_read_errors;
        return;
    }

    const int n = buf[0] & 0x0F;
    if (n == 0 || n > 2) {
        if (s_finger_down) {
            s_finger_down = false;
            publish_ui(false, 0, 0, 0, 0);
            if (s_cb != NULL) {
                const cm_touch_point_t none = {0};
                s_cb(&none, false, CM_VK_NONE);
            }
        }
        return;
    }

    const int ev = (buf[1] >> 6) & 0x03;
    if (ev != FT_EVENT_DOWN && ev != FT_EVENT_CONTACT) {
        return;
    }

    const int raw_x = ((buf[1] & 0x0F) << 8) | buf[2];
    const int raw_y = ((buf[3] & 0x0F) << 8) | buf[4];
    s_last_raw_x = raw_x;
    s_last_raw_y = raw_y;

    int lx = 0, ly = 0;
    map_to_logical(raw_x, raw_y, &lx, &ly);
    s_finger_down = true;

    const cm_vk_t vk = cm_touch_classify_vk(raw_x, raw_y);

    if (s_cb != NULL) {
        const cm_touch_point_t pt = {
            .pressed = true, .x = lx, .y = ly, .raw_x = raw_x, .raw_y = raw_y};
        s_cb(&pt, true, vk);
    }
    publish_ui(true, lx, ly, raw_x, raw_y);
}

static void touch_feed_task(void *arg) {
    (void)arg;
    for (;;) {
        if (s_feed_pause_depth > 0) {
            while (xSemaphoreTake(s_irq_sem, 0) == pdTRUE) {
            }
            vTaskDelay(pdMS_TO_TICKS(TOUCH_FEED_MS));
            continue;
        }

        /* Shorter period while a finger is down so moves are tracked. */
        const TickType_t wait = pdMS_TO_TICKS(s_finger_down ? TOUCH_FEED_MS : 20);
        xSemaphoreTake(s_irq_sem, wait);

        if (s_feed_pause_depth > 0) {
            continue;
        }
        while (xSemaphoreTake(s_irq_sem, 0) == pdTRUE) {
        }

        feed_one_sample();

        /* INT still low means the finger is still down: take a second sample
         * rather than waiting for the timeout. */
        if (gpio_get_level(METALIO_TOUCH_INT_PIN) == 0) {
            feed_one_sample();
        }
    }
}

bool cm_touch_init(void) {
    s_ok = false;
    s_finger_down = false;
    s_ui_pressed = false;
    s_ui_seen = false;
    s_replay_press = false;
    s_replay_release = false;
    s_feed_pause_depth = 0;

    if (s_irq_sem == NULL) {
        s_irq_sem = xSemaphoreCreateBinary();
        if (s_irq_sem == NULL) {
            ESP_LOGE(TAG, "cannot create IRQ semaphore");
            return false;
        }
    }

    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << (unsigned)METALIO_TOUCH_INT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io);

    /* Reset goes through TCA P1.2; the board layer owns that shared line. */
    cm_board_touch_reset();

    uint8_t chip = 0;
    uint8_t fw = 0;
    const esp_err_t id_err = ft_read(FT_REG_CHIP_ID, &chip, 1);
    ft_read(FT_REG_FW_VER, &fw, 1);

    if (id_err != ESP_OK) {
        ESP_LOGE(TAG, "no ACK at 0x%02X (INT=%d): %s", METALIO_TOUCH_I2C_ADDR,
                 gpio_get_level(METALIO_TOUCH_INT_PIN), esp_err_to_name(id_err));
        return false;
    }

    /* 0x00: hold INT low for the whole press, so the falling edge starts a feed. */
    ft_write(FT_REG_INT_MODE, 0x00);

    esp_err_t isr = gpio_install_isr_service(0);
    if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "gpio_install_isr_service: %s", esp_err_to_name(isr));
    }
    gpio_isr_handler_remove(METALIO_TOUCH_INT_PIN);
    if (gpio_isr_handler_add(METALIO_TOUCH_INT_PIN, touch_isr, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "isr add failed on GPIO%d", (int)METALIO_TOUCH_INT_PIN);
        return false;
    }

    if (s_feed_task == NULL) {
        if (xTaskCreate(touch_feed_task, "cm_touch", TOUCH_FEED_STACK, NULL,
                        TOUCH_FEED_PRIO, &s_feed_task) != pdPASS) {
            ESP_LOGE(TAG, "cannot create sampling task");
            gpio_isr_handler_remove(METALIO_TOUCH_INT_PIN);
            return false;
        }
    }

    /* INT may already be low at power-up; kick once so the first tap is seen. */
    if (gpio_get_level(METALIO_TOUCH_INT_PIN) == 0) {
        xSemaphoreGive(s_irq_sem);
    }

    s_ok = true;
    ESP_LOGI(TAG, "ready chip=0x%02X%s fw=0x%02X INT=GPIO%d", chip,
             chip == FT_CHIP_ID_6336 ? " (FT6336)" : "", fw,
             (int)METALIO_TOUCH_INT_PIN);
    return true;
}

bool cm_touch_ok(void) { return s_ok; }

void cm_touch_set_callback(cm_touch_cb_t cb) { s_cb = cb; }

void cm_touch_set_feed_paused(bool paused) {
    portENTER_CRITICAL(&s_mux);
    bool edge_resume = false;
    if (paused) {
        if (s_feed_pause_depth < 100) {
            s_feed_pause_depth += 1;
        }
    } else if (s_feed_pause_depth > 0) {
        s_feed_pause_depth -= 1;
        edge_resume = (s_feed_pause_depth == 0);
    }
    portEXIT_CRITICAL(&s_mux);

    if (edge_resume && s_irq_sem != NULL &&
        gpio_get_level(METALIO_TOUCH_INT_PIN) == 0) {
        xSemaphoreGive(s_irq_sem);
    }
}

bool cm_touch_read(cm_touch_point_t *out) {
    if (!s_ok) {
        return false;
    }
    bool pressed = false;
    cm_touch_point_t pt = {0};

    portENTER_CRITICAL(&s_mux);
    if (s_replay_press) {
        s_replay_press = false;
        pressed = true;
        pt = s_ui_pt;
    } else if (s_ui_pressed) {
        s_ui_seen = true;
        pressed = true;
        pt = s_ui_pt;
    } else if (s_replay_release) {
        s_replay_release = false;
        pressed = false;
    }
    portEXIT_CRITICAL(&s_mux);

    if (!pressed) {
        return false;
    }
    if (out != NULL) {
        *out = pt;
    }
    return true;
}