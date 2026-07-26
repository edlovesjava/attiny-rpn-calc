#include "led_core.h"

void led_core_init(led_core_t *l, uint8_t count)
{
    l->count      = (count > LED_CORE_MAX) ? LED_CORE_MAX : count;
    l->state      = 0;
    l->brightness = 0xFF;   /* full on; hosts dim deliberately */
    l->blink      = 0;
    l->blink_ms10 = 50;     /* 500 ms — a sane default if a host enables blink */
    l->blink_duty = 0x80;   /* 50 % */
    l->phase      = 0;
}

/* Mask of populated LED positions, so a host cannot set bits for LEDs that do
 * not exist and then read them back as if they did. */
static uint8_t populated(const led_core_t *l)
{
    return (l->count >= 8) ? 0xFF : (uint8_t)((1u << l->count) - 1u);
}

uint8_t led_core_read(uint8_t reg, void *ctx)
{
    const led_core_t *l = (const led_core_t *)ctx;

    switch (reg) {
    case SMARTINY_LED_REG_COUNT:      return l->count;
    case SMARTINY_LED_REG_STATE:      return l->state;
    case SMARTINY_LED_REG_BRIGHTNESS: return l->brightness;
    case SMARTINY_LED_REG_BLINK:      return l->blink;
    case SMARTINY_LED_REG_BLINK_MS:   return l->blink_ms10;
    case SMARTINY_LED_REG_BLINK_DUTY: return l->blink_duty;
    default:                          return 0x00;
    }
}

void led_core_write(uint8_t reg, uint8_t val, void *ctx)
{
    led_core_t *l = (led_core_t *)ctx;

    switch (reg) {
    case SMARTINY_LED_REG_STATE:
        l->state = val & populated(l);
        break;
    case SMARTINY_LED_REG_BRIGHTNESS:
        l->brightness = val;
        break;
    case SMARTINY_LED_REG_BLINK:
        l->blink = val & populated(l);
        break;
    case SMARTINY_LED_REG_BLINK_MS:
        l->blink_ms10 = val;
        break;
    case SMARTINY_LED_REG_BLINK_DUTY:
        l->blink_duty = val;
        break;
    default:
        break;   /* COUNT is read-only; RGB/PATTERN are v2 */
    }
}

/* Open while the blink phase is in its on-fraction. Wide open when blinking is
 * disabled, so a zero period simply means "no gating" rather than "always off". */
static uint8_t blink_gate(const led_core_t *l, uint16_t now_ms)
{
    if (l->blink_ms10 == 0)
        return 0xFF;

    uint16_t period = (uint16_t)l->blink_ms10 * SMARTINY_LED_BLINK_MS_UNIT;
    uint16_t phase  = (uint16_t)(now_ms % period);
    uint16_t on_ms  = (uint16_t)(((uint32_t)period * l->blink_duty) / 255u);

    return (phase < on_ms) ? 0xFF : 0x00;
}

uint8_t led_core_tick(led_core_t *l, uint16_t now_ms)
{
    /* Steady LEDs follow STATE; blinking LEDs additionally pass the gate. */
    uint8_t gate = blink_gate(l, now_ms);
    uint8_t lit  = (uint8_t)((l->state & (uint8_t)~l->blink) |
                             (l->state & l->blink & gate));

    /* Duty = brightness/256, so 0 is fully dark and 255 is on 255/256 of the
     * time. Comparing before the increment keeps brightness 0 truly off. */
    uint8_t on = (l->phase < l->brightness) ? lit : 0x00;
    l->phase++;
    return on;
}
