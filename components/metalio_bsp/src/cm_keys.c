/**
 * @file cm_keys.c
 * @brief CrossMetal button input implementation.
 *
 * Ported from Metalio-E-INK4-Plus `input/metalio_keys.cc` (MIT).
 *
 * The scanner wakes on either the BOOT GPIO interrupt or the expander INT, then
 * re-reads both. A 50 ms tick doubles as the BOOT poll and the long-press
 * cadence, so a missed edge still resolves within one tick.
 */

#include "cm_keys.h"

#include "cm_board.h"
#include "cm_tca9555.h"
#include "metalio_pinout.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

static const char *TAG = "cm_keys";

#define CM_KEY_DEBOUNCE_US 30000
#define CM_KEY_POWER_LONG_US (METALIO_POWER_LONG_PRESS_MS * 1000LL)
#define CM_KEY_QUEUE_LEN 8
#define CM_KEY_TICK_MS 50

typedef struct {
    const char *label;
    int io; /* <0: the BOOT GPIO; >=0: a TCA9555 pin number */
    bool idle_high;
    bool prev_down;
    int64_t last_change_us;
} key_slot_t;

static key_slot_t s_keys[CM_KEY_COUNT] = {
    {"BOOT", -1, true, false, 0},
    {"VOL+", EPD_IO_VOL_UP, true, false, 0},
    {"VOL-", EPD_IO_VOL_DOWN, true, false, 0},
    {"POWER", EPD_IO_POWER, true, false, 0},
};

static volatile bool s_ready;
static volatile bool s_power_click;
static int64_t s_power_down_us;
static bool s_power_long_fired;
static volatile bool s_shutdown_pulse_req;

static SemaphoreHandle_t s_sample_mu;
static StaticSemaphore_t s_irq_sem_storage;
static SemaphoreHandle_t s_irq_sem;
static QueueHandle_t s_evt_q;
static TaskHandle_t s_keys_task;
static uint8_t s_p1 = 0xFF;

static bool level_high(const key_slot_t *k) {
    if (k->io < 0) {
        return gpio_get_level(METALIO_BOOT_BUTTON_GPIO) != 0;
    }
    return (s_p1 & (1u << (k->io & 7))) != 0;
}

static bool is_down(const key_slot_t *k) {
    return level_high(k) != k->idle_high;
}

/* Re-read Port1, then Port0 purely to clear the expander's interrupt latch. */
static void refresh_p1(void) {
    uint8_t p1 = s_p1;
    if (cm_tca9555_read_input(1, &p1) == ESP_OK) {
        s_p1 = p1;
    }
    uint8_t p0 = 0;
    cm_tca9555_read_input(0, &p0);
}

static void push_edge(void) {
    if (s_evt_q == NULL) {
        return;
    }
    const uint8_t one = 1;
    xQueueSend(s_evt_q, &one, 0);
}

static void apply_edge(key_slot_t *k, bool down, int64_t now) {
    if (down == k->prev_down) {
        return;
    }
    if (now - k->last_change_us < CM_KEY_DEBOUNCE_US) {
        return;
    }
    k->last_change_us = now;
    k->prev_down = down;

    ESP_LOGI(TAG, "%s %s", k->label, down ? "DOWN" : "UP");

    if (k == &s_keys[CM_KEY_POWER]) {
        if (down) {
            s_power_down_us = now;
            s_power_long_fired = false;
        } else {
            if (s_power_down_us != 0 && !s_power_long_fired) {
                s_power_click = true;
            }
            s_power_down_us = 0;
            s_power_long_fired = false;
        }
    }
    push_edge();
}

static void sample_all(void) {
    const int64_t now = esp_timer_get_time();
    refresh_p1();
    for (int i = 0; i < CM_KEY_COUNT; ++i) {
        apply_edge(&s_keys[i], is_down(&s_keys[i]), now);
    }

    if (s_power_down_us != 0 && !s_power_long_fired &&
        is_down(&s_keys[CM_KEY_POWER]) &&
        (now - s_power_down_us) >= CM_KEY_POWER_LONG_US) {
        s_power_long_fired = true;
        ESP_LOGI(TAG, "POWER long press %d ms -> shutdown pulse",
                 METALIO_POWER_LONG_PRESS_MS);
        s_shutdown_pulse_req = true;
    }
}

static void IRAM_ATTR boot_isr(void *arg) {
    (void)arg;
    BaseType_t hp = pdFALSE;
    if (s_irq_sem != NULL) {
        xSemaphoreGiveFromISR(s_irq_sem, &hp);
    }
    if (hp == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void setup_boot_gpio(void) {
    gpio_reset_pin(METALIO_BOOT_BUTTON_GPIO);
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << (unsigned)METALIO_BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io);
    gpio_isr_handler_add(METALIO_BOOT_BUTTON_GPIO, boot_isr, NULL);
}

static void keys_task(void *arg) {
    (void)arg;
    while (true) {
        /* Wait for the BOOT ISR or the expander INT, at most one tick. */
        if (s_irq_sem != NULL) {
            xSemaphoreTake(s_irq_sem, pdMS_TO_TICKS(CM_KEY_TICK_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(CM_KEY_TICK_MS));
        }

        /* Drain any edges that arrived while we were sampling. */
        while (s_irq_sem != NULL && xSemaphoreTake(s_irq_sem, 0) == pdTRUE) {
        }

        xSemaphoreTake(s_sample_mu, portMAX_DELAY);
        if (s_ready) {
            sample_all();
        }
        xSemaphoreGive(s_sample_mu);
    }
}

void cm_keys_init(void) {
    if (s_keys_task != NULL) {
        return;
    }

    if (s_sample_mu == NULL) {
        s_sample_mu = xSemaphoreCreateMutex();
    }
    if (s_irq_sem == NULL) {
        s_irq_sem = xSemaphoreCreateBinaryStatic(&s_irq_sem_storage);
    }

    setup_boot_gpio();

    /* Two passes: the expander's first read after power-up can be stale. */
    refresh_p1();
    vTaskDelay(pdMS_TO_TICKS(20));
    refresh_p1();

    const int64_t now = esp_timer_get_time();
    for (int i = 0; i < CM_KEY_COUNT; ++i) {
        /* Expander keys rest high by hardware; do not infer idle from a press. */
        s_keys[i].idle_high = (s_keys[i].io < 0) ? level_high(&s_keys[i]) : true;
        s_keys[i].prev_down = is_down(&s_keys[i]);
        s_keys[i].last_change_us = now;
    }
    s_power_down_us = 0;
    s_power_long_fired = false;
    s_shutdown_pulse_req = false;

    if (s_evt_q == NULL) {
        s_evt_q = xQueueCreate(CM_KEY_QUEUE_LEN, sizeof(uint8_t));
    }
    s_ready = true;

    ESP_LOGI(TAG, "BOOT=GPIO%d TCA_INT=GPIO%d P1=0x%02X", METALIO_BOOT_BUTTON_GPIO,
             METALIO_IO_EXPANDER_INT_GPIO, s_p1);

    xTaskCreate(keys_task, "cm_keys", 3072, NULL, 5, &s_keys_task);
}

void cm_keys_mask_for_sleep(void) {
    /*
     * Stop scanning BEFORE the board layer changes pin directions. The expander
     * key pins double as outputs, so driving them low while the scanner still
     * runs is sampled as a press, and the release on wake then emits a spurious
     * page turn.
     */
    xSemaphoreTake(s_sample_mu, portMAX_DELAY);
    s_ready = false;
    cm_board_ioexp_mask_for_sleep();
    xSemaphoreGive(s_sample_mu);
    ESP_LOGI(TAG, "expander masked for sleep");
}

void cm_keys_resume_after_sleep(void) {
    xSemaphoreTake(s_sample_mu, portMAX_DELAY);
    s_shutdown_pulse_req = false;
    s_power_down_us = 0;
    s_power_long_fired = false;

    /*
     * The board layer owns the port images, including the gated rails on P1.2
     * and P1.4. Restoring directions here would clear those bits and take the
     * touch controller's reset line down with them, which silently kills touch.
     */
    cm_board_ioexp_restore_after_sleep();

    /* Pull-ups need a moment to settle before the scanner trusts a level. */
    vTaskDelay(pdMS_TO_TICKS(20));
    refresh_p1();

    const int64_t now = esp_timer_get_time();
    for (int i = 0; i < CM_KEY_COUNT; ++i) {
        s_keys[i].prev_down = is_down(&s_keys[i]);
        s_keys[i].last_change_us = now;
    }
    s_ready = true;
    xSemaphoreGive(s_sample_mu);
}

bool cm_keys_ready(void) { return s_ready; }

bool cm_keys_take_power_click(void) {
    if (!s_power_click) {
        return false;
    }
    s_power_click = false;
    return true;
}

bool cm_keys_has_power_click(void) { return s_power_click; }

bool cm_keys_take_edge(void) {
    if (!s_ready || s_evt_q == NULL) {
        return false;
    }
    uint8_t one = 0;
    return xQueueReceive(s_evt_q, &one, 0) == pdTRUE;
}

bool cm_keys_is_down(int index) {
    if (index < 0 || index >= CM_KEY_COUNT || !s_ready) {
        return false;
    }
    xSemaphoreTake(s_sample_mu, portMAX_DELAY);
    /* Read live: standby-timing logic must not trust a cached edge. */
    if (s_keys[index].io >= 0) {
        refresh_p1();
    }
    const bool down = is_down(&s_keys[index]);
    xSemaphoreGive(s_sample_mu);
    return down;
}

bool cm_keys_take_shutdown_pulse(void) {
    if (!s_shutdown_pulse_req) {
        return false;
    }
    s_shutdown_pulse_req = false;
    return true;
}