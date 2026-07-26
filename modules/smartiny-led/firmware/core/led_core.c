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
    for (uint8_t i = 0; i < LED_CORE_MAX; i++)
        l->level[i] = SMARTINY_LED_LEVEL_MAX;   /* full on unless dimmed */
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
    default: break;
    }
    if (reg >= SMARTINY_LED_REG_LEVEL_BASE &&
        reg <  SMARTINY_LED_REG_LEVEL_BASE + (LED_CORE_MAX / 2)) {
        uint8_t i = (uint8_t)((reg - SMARTINY_LED_REG_LEVEL_BASE) * 2);
        return (uint8_t)(l->level[i] | (l->level[i + 1] << 4));
    }
    return 0x00;
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
        /* Per-LED 4-bit levels, two LEDs per register. */
        if (reg >= SMARTINY_LED_REG_LEVEL_BASE &&
            reg <  SMARTINY_LED_REG_LEVEL_BASE + (LED_CORE_MAX / 2)) {
            uint8_t i = (uint8_t)((reg - SMARTINY_LED_REG_LEVEL_BASE) * 2);
            l->level[i]     = SMARTINY_LED_LEVEL_GET(val, 0);
            l->level[i + 1] = SMARTINY_LED_LEVEL_GET(val, 1);
        }
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

uint8_t led_core_is_static(const led_core_t *l)
{
    if (l->blink & l->state)          return 0;   /* something is blinking */
    if (l->brightness != 0xFF)        return 0;   /* master level is dimmed */
    for (uint8_t i = 0; i < LED_CORE_MAX; i++)
        if ((l->state & (1u << i)) && l->level[i] != SMARTINY_LED_LEVEL_MAX)
            return 0;                             /* a lit LED is dimmed */
    return 1;
}

uint8_t led_core_tick(led_core_t *l, uint16_t now_ms)
{
    /* Steady LEDs follow STATE; blinking LEDs additionally pass the gate. */
    uint8_t gate = blink_gate(l, now_ms);
    uint8_t lit  = (uint8_t)((l->state & (uint8_t)~l->blink) |
                             (l->state & l->blink & gate));

    /* Per-LED 4-bit PWM on the low nibble of the phase counter. Sharing one
     * counter with the master PWM below keeps the two in lockstep, so they
     * cannot beat against each other and produce visible flicker. */
    uint8_t sub = (uint8_t)(l->phase & 0x0F);
    for (uint8_t i = 0; i < LED_CORE_MAX; i++)
        if (l->level[i] != SMARTINY_LED_LEVEL_MAX && l->level[i] <= sub)
            lit &= (uint8_t)~(1u << i);   /* MAX bypasses gating entirely, so
                                           * "full" really is 100 %, not 15/16 */

    /* Master brightness: duty = brightness/256, so 0 is fully dark and 255 is
     * on 255/256 of the time. Comparing before the increment keeps 0 off. */
    uint8_t on = (l->phase < l->brightness) ? lit : 0x00;
    l->phase++;
    return on;
}
