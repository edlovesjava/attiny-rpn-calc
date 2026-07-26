/*
 * smartiny_regs.h — the shared I2C register model for every smartiny module.
 *
 * Each module implements the same common header (0x00–0x0F) so a host can
 * identify and configure any module uniformly, whatever it actually does.
 * Module-specific registers start at SMARTINY_REG_MODULE_BASE.
 *
 * This header is the single source of truth shared by module firmware, the RPN
 * host, and the PC-side test harness — keep it dependency-free.
 *
 * See docs/architecture.md §5.
 */
#ifndef SMARTINY_REGS_H
#define SMARTINY_REGS_H

#include <stdint.h>

/* ---- common header: implemented by EVERY module -------------------------- */
#define SMARTINY_REG_WHO_AM_I     0x00  /* R   device type, see ids below      */
#define SMARTINY_REG_VERSION      0x01  /* R   packed major.minor (4b|4b)      */
#define SMARTINY_REG_STATUS       0x02  /* R   generic status bits             */
#define SMARTINY_REG_CONFIG       0x03  /* R/W generic config bits             */
#define SMARTINY_REG_I2C_ADDR     0x04  /* R/W 7-bit addr, persisted to EEPROM */
#define SMARTINY_REG_MODULE_BASE  0x10  /* module-specific registers start     */

#define SMARTINY_VERSION_PACK(maj, min) \
    ((uint8_t)((((maj) & 0x0F) << 4) | ((min) & 0x0F)))

/* ---- WHO_AM_I device type ids -------------------------------------------- */
/* 0x00 and 0xFF are deliberately INVALID: a bus stuck low reads 0x00 and an
 * absent device on a pulled-up bus reads 0xFF, so neither can be mistaken for a
 * real module during enumeration. */
#define SMARTINY_ID_INVALID_LOW   0x00
#define SMARTINY_ID_KEY           0x01  /* smartiny-key  — 16-key keypad       */
#define SMARTINY_ID_LED           0x02  /* smartiny-led  — indicator / light   */
#define SMARTINY_ID_PWR           0x03  /* smartiny-pwr  — LiPo telemetry      */
#define SMARTINY_ID_MEM           0x04  /* smartiny-mem  — managed NV store    */
#define SMARTINY_ID_CALC          0x05  /* smartiny-calc — RPN host            */
#define SMARTINY_ID_INVALID_HIGH  0xFF

#define SMARTINY_ID_IS_VALID(id) \
    ((id) != SMARTINY_ID_INVALID_LOW && (id) != SMARTINY_ID_INVALID_HIGH)

/* ---- default 7-bit bus addresses ----------------------------------------- */
#define SMARTINY_ADDR_KEY   0x20
#define SMARTINY_ADDR_LED   0x21
#define SMARTINY_ADDR_PWR   0x22
#define SMARTINY_ADDR_OLED  0x3C  /* off-the-shelf SSD1306                    */
#define SMARTINY_ADDR_FRAM  0x50  /* raw FRAM/EEPROM — no common header       */

/* ---- STATUS bits (common) ------------------------------------------------ */
#define SMARTINY_STATUS_EVENTS    (1u << 0)  /* module has queued events       */
#define SMARTINY_STATUS_OVERFLOW  (1u << 1)  /* event FIFO overflowed          */
#define SMARTINY_STATUS_FAULT     (1u << 2)  /* module-specific fault          */
#define SMARTINY_STATUS_ALERT     (1u << 3)  /* asserting the shared INT line  */

/* ---- smartiny-key -------------------------------------------------------- */
#define SMARTINY_KEY_REG_STATUS      0x10  /* R                               */
#define SMARTINY_KEY_REG_MODIFIERS   0x11  /* R   live modifier latch bitmask */
#define SMARTINY_KEY_REG_EVENT_FIFO  0x12  /* R   read pops one event byte    */
#define SMARTINY_KEY_REG_EVENT_COUNT 0x13  /* R   queued events               */
#define SMARTINY_KEY_REG_DEBOUNCE_MS 0x14  /* R/W                             */
#define SMARTINY_KEY_REG_REPEAT_CFG  0x15  /* R/W key-repeat enable / rate    */
#define SMARTINY_KEY_REG_LED_LOCAL   0x16  /* R/W LED when LED_MANUAL is set  */
#define SMARTINY_KEY_REG_HOLD_MS     0x17  /* R/W long-press threshold, ×10ms */
#define SMARTINY_KEY_REG_MOD0_CFG    0x18  /* R/W see MOD_CFG packing below   */
#define SMARTINY_KEY_REG_MOD1_CFG    0x19
#define SMARTINY_KEY_REG_MOD2_CFG    0x1A
#define SMARTINY_KEY_REG_MOD3_CFG    0x1B
#define SMARTINY_KEY_REG_LED_MODE    0x1C  /* R/W local-LED behaviour         */
#define SMARTINY_KEY_REG_SAVE        0x1F  /* W   commit config to EEPROM     */

/* Event byte layout: [type:2][mods:2][keycode:4].
 * mods is the modifier latch state AT THE MOMENT of the event, so the host
 * always knows the context a key was pressed in.
 *
 * The module reports FACTS and the host assigns MEANING: a short tap yields
 * PRESS + RELEASE, a long hold yields PRESS + LONG + RELEASE. The host decides
 * what "long press" does — the keypad never guesses. */
#define SMARTINY_EVT_KEYCODE(e)     ((uint8_t)((e) & 0x0F))
#define SMARTINY_EVT_MODS(e)        ((uint8_t)(((e) >> 4) & 0x03))
#define SMARTINY_EVT_TYPE(e)        ((uint8_t)(((e) >> 6) & 0x03))
#define SMARTINY_EVT_PACK(type, mods, key) \
    ((uint8_t)((((type) & 0x03u) << 6) | (((mods) & 0x03u) << 4) | ((key) & 0x0Fu)))

#define SMARTINY_EVT_PRESS    0u  /* key settled down                          */
#define SMARTINY_EVT_RELEASE  1u  /* key returned to idle                      */
#define SMARTINY_EVT_LONG     2u  /* still held at HOLD_MS — fires while held  */
#define SMARTINY_EVT_REPEAT   3u  /* auto-repeat tick                          */

/* Modifier config byte: [mode:4][keycode:4] — any of the 16 keys can be a
 * modifier, stored in EEPROM, so the pad layout is not baked in. */
#define SMARTINY_MOD_CFG_PACK(mode, key) \
    ((uint8_t)((((mode) & 0x0Fu) << 4) | ((key) & 0x0Fu)))
#define SMARTINY_MOD_CFG_MODE(c)  ((uint8_t)(((c) >> 4) & 0x0F))
#define SMARTINY_MOD_CFG_KEY(c)   ((uint8_t)((c) & 0x0F))

#define SMARTINY_MOD_OFF        0u  /* slot unused                             */
#define SMARTINY_MOD_MOMENTARY  1u  /* active only while physically held       */
#define SMARTINY_MOD_STICKY     2u  /* one-shot: applies to next key, then clears */
#define SMARTINY_MOD_LOCK       3u  /* toggles until pressed again (caps-lock) */
#define SMARTINY_MOD_TAPHOLD    4u  /* dual-purpose: tap = normal key,
                                     * hold = toggle the modifier. The key's
                                     * normal function is deferred to release,
                                     * so a TAPHOLD key cannot auto-repeat. */

/* LED_MODE bits */
#define SMARTINY_KEY_LED_TALKBACK  (1u << 0)  /* solid while a key is held    */
#define SMARTINY_KEY_LED_MODIFIER  (1u << 1)  /* blink while a modifier is up */
#define SMARTINY_KEY_LED_MANUAL    (1u << 7)  /* host drives LED_LOCAL itself */

#define SMARTINY_KEY_SAVE_MAGIC  0x5Au  /* write to SAVE to persist config    */

#define SMARTINY_KEY_NONE  0xFF  /* no key / idle */

/* ---- smartiny-led -------------------------------------------------------- */
#define SMARTINY_LED_REG_COUNT       0x10  /* R   LEDs present                */
#define SMARTINY_LED_REG_STATE       0x11  /* R/W on/off bitmask              */
#define SMARTINY_LED_REG_BRIGHTNESS  0x12  /* R/W global brightness           */
#define SMARTINY_LED_REG_RGB_BASE    0x13  /* R/W per-LED RGB, 3 bytes each   */
#define SMARTINY_LED_REG_PATTERN     0x1F  /* R/W built-in pattern (smart fw) */

/* ---- smartiny-pwr -------------------------------------------------------- */
#define SMARTINY_PWR_REG_BATT_MV_L   0x10  /* R   battery mV, little-endian   */
#define SMARTINY_PWR_REG_BATT_MV_H   0x11
#define SMARTINY_PWR_REG_SOURCE      0x12  /* R   0=batt 1=barrel 2=usb       */
#define SMARTINY_PWR_REG_CHARGE      0x13  /* R   idle/charging/full/fault    */
#define SMARTINY_PWR_REG_BATT_PCT    0x14  /* R   estimated %                 */
#define SMARTINY_PWR_REG_RAIL_CTRL   0x15  /* R/W rail enable, low-power mode */
#define SMARTINY_PWR_REG_FLAGS       0x16  /* R   low-batt / over-temp / fault*/

#endif /* SMARTINY_REGS_H */
