/**
 * @file pca9555.h
 * @brief Compatibility shim: upstream `pca9555.h` -> `cm_tca9555.h`.
 *
 * Upstream's wrappers take a port argument first; this board has one expander,
 * so it is accepted and ignored. The bit-mask constants are re-exported under
 * their upstream names because the board glue composes masks from them.
 */

#ifndef CROSSMETAL_SHIM_PCA9555_H
#define CROSSMETAL_SHIM_PCA9555_H

#include "cm_board.h"
#include "cm_tca9555.h"

#include <driver/i2c_master.h> /* i2c_port_t, used by the upstream signatures */

#define PCA_PIN_P00 CM_TCA_PIN_P00
#define PCA_PIN_P01 CM_TCA_PIN_P01
#define PCA_PIN_P02 CM_TCA_PIN_P02
#define PCA_PIN_P03 CM_TCA_PIN_P03
#define PCA_PIN_P04 CM_TCA_PIN_P04
#define PCA_PIN_P05 CM_TCA_PIN_P05
#define PCA_PIN_P06 CM_TCA_PIN_P06
#define PCA_PIN_P07 CM_TCA_PIN_P07
#define PCA_PIN_PC10 CM_TCA_PIN_P10
#define PCA_PIN_PC11 CM_TCA_PIN_P11
#define PCA_PIN_PC12 CM_TCA_PIN_P12
#define PCA_PIN_PC13 CM_TCA_PIN_P13
#define PCA_PIN_PC14 CM_TCA_PIN_P14
#define PCA_PIN_PC15 CM_TCA_PIN_P15
#define PCA_PIN_PC16 CM_TCA_PIN_P16
#define PCA_PIN_PC17 CM_TCA_PIN_P17
#define PCA_PIN_P_ALL CM_TCA_PIN_P0_ALL
#define PCA_PIN_PC_ALL CM_TCA_PIN_P1_ALL

static inline void pca9555_set_addr(uint8_t addr_7bit) {
    cm_tca9555_set_addr(addr_7bit);
}

static inline uint8_t pca9555_get_addr(void) { return cm_tca9555_get_addr(); }

static inline esp_err_t pca9555_set_config(i2c_port_t port, uint8_t config_value,
                                           int high_port) {
    (void)port;
    return cm_tca9555_set_config(config_value, high_port);
}

static inline esp_err_t pca9555_set_inversion(i2c_port_t port, uint8_t config_value,
                                              int high_port) {
    (void)port;
    return cm_tca9555_set_inversion(config_value, high_port);
}

/**
 * Output-register writes are routed through the board's shared port image.
 *
 * The vendored display glue keeps its own shadow of both expander ports. So does
 * `cm_board`. Two shadows of one register is a real defect, not a style issue:
 * whichever layer wrote last wins, and a stale shadow in one silently clobbers
 * the other's bits. Concretely, after `epd_poweroff()` the board's image still
 * believed the panel was powered, so a later motor or camera-rail write would
 * re-energise the panel.
 *
 * Funnelling writes through `cm_board_ioexp_write_port()` keeps one authoritative
 * image. This is why the vendored file needs no edits.
 *
 * Scoped to `EPDIO_PCA9555_BOARD_OWNED` so this header can also be used by code
 * that legitimately wants the raw register.
 */
#ifdef EPDIO_PCA9555_BOARD_OWNED
static inline esp_err_t pca9555_set_value(i2c_port_t port, uint8_t config_value,
                                          int high_port) {
    (void)port;
    /* Bits the display driver owns on each port. */
    const uint8_t mask = (high_port == 0)
                             ? (uint8_t)(CM_BOARD_P0_OUT_MASK)
                             : (uint8_t)(CM_BOARD_P1_OUT_MASK);
    return cm_board_ioexp_write_port(high_port, config_value, mask);
}
#else
static inline esp_err_t pca9555_set_value(i2c_port_t port, uint8_t config_value,
                                          int high_port) {
    (void)port;
    return cm_tca9555_set_value(config_value, high_port);
}
#endif

static inline esp_err_t pca9555_read_input_ex(i2c_port_t port, int high_port,
                                              uint8_t *out) {
    (void)port;
    return cm_tca9555_read_input(high_port, out);
}

static inline uint8_t pca9555_read_input(i2c_port_t port, int high_port) {
    (void)port;
    return cm_tca9555_read_input_raw(high_port);
}

#endif /* CROSSMETAL_SHIM_PCA9555_H */