/*
 * led_core.h — smartiny-led application logic. Pure C, no hardware.
 *
 * Owns LED on/off state and global soft-PWM brightness, and exposes the
 * module-specific register block to the shared slave engine. The platform layer
 * only has to call led_core_tick() and push the returned bitmask at the pins.
 */
#ifndef LED_CORE_H
#define LED_CORE_H

#include <stdint.h>
#include "../../../../lib/smartiny-common/smartiny_regs.h"

#define LED_CORE_MAX  8   /* bitmask is one byte */

typedef struct {
    uint8_t count;        /* how many LEDs this board actually populates */
    uint8_t state;        /* on/off bitmask, bit n = LED n                */
    uint8_t brightness;   /* global soft-PWM level, 0..255                */
    uint8_t phase;        /* free-running PWM phase counter               */
} led_core_t;

void led_core_init(led_core_t *l, uint8_t count);

/* Register callbacks for smartiny_slave (signature matches smartiny_read_fn /
 * smartiny_write_fn; ctx is a led_core_t*). */
uint8_t led_core_read(uint8_t reg, void *ctx);
void    led_core_write(uint8_t reg, uint8_t val, void *ctx);

/* Advance the PWM phase by one step and return the bitmask that should be
 * driven RIGHT NOW. Call it fast and steadily from the main loop — never from
 * an ISR, so it can never delay a USI I2C transaction. */
uint8_t led_core_tick(led_core_t *l);

#endif /* LED_CORE_H */
