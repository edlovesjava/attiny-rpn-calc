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
    uint8_t  count;       /* how many LEDs this board actually populates  */
    uint8_t  state;       /* on/off bitmask, bit n = LED n                */
    uint8_t  brightness;  /* global soft-PWM level, 0..255                */
    uint8_t  blink;       /* bitmask: LEDs gated by the blink phase       */
    uint8_t  blink_ms10;  /* blink period in units of 10 ms; 0 = disabled */
    uint8_t  blink_duty;  /* on-fraction of the period, 0..255            */
    uint8_t  level[LED_CORE_MAX]; /* per-LED brightness 0..15, 15 = full   */
    uint8_t  phase;       /* free-running PWM phase counter               */
} led_core_t;

void led_core_init(led_core_t *l, uint8_t count);

/* Register callbacks for smartiny_slave (signature matches smartiny_read_fn /
 * smartiny_write_fn; ctx is a led_core_t*). */
uint8_t led_core_read(uint8_t reg, void *ctx);
void    led_core_write(uint8_t reg, uint8_t val, void *ctx);

/* Advance the PWM phase by one step and return the bitmask that should be
 * driven RIGHT NOW. Call it fast and steadily from the main loop — never from
 * an ISR, so it can never delay a USI I2C transaction.
 *
 * now_ms is a free-running millisecond counter (millis() on the target, a
 * synthetic value under test). It drives blink gating only; PWM advances once
 * per call regardless, so PWM frequency follows loop speed. */
uint8_t led_core_tick(led_core_t *l, uint16_t now_ms);

/* True when nothing needs animating — every lit LED is at full level, global
 * brightness is full, and no LED is blinking. A shift-register driver should
 * write the register once and stop refreshing while this holds, which restores
 * the 595's set-and-forget behaviour (and lets the MCU sleep). */
uint8_t led_core_is_static(const led_core_t *l);

#endif /* LED_CORE_H */
