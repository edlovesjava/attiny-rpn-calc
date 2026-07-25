#include "led_core.h"

void led_core_init(led_core_t *l, uint8_t count)
{
    l->count      = (count > LED_CORE_MAX) ? LED_CORE_MAX : count;
    l->state      = 0;
    l->brightness = 0xFF;   /* full on; hosts dim deliberately */
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
    default:
        break;   /* COUNT is read-only; RGB/PATTERN are v2 */
    }
}

uint8_t led_core_tick(led_core_t *l)
{
    /* Duty = brightness/256, so 0 is fully dark and 255 is on 255/256 of the
     * time. Comparing before the increment keeps brightness 0 truly off. */
    uint8_t on = (l->phase < l->brightness) ? l->state : 0x00;
    l->phase++;
    return on;
}
