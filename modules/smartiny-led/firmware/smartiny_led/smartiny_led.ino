/*
 * smartiny-led — the reference smartiny I2C slave.
 *
 * Target: ATtiny85 @ 8 MHz internal RC, 3.3 V (works at 5 V for bench work).
 * Requires: ATTinyCore board package + the TinyWireS library.
 *
 *   PB0  SDA (USI)          PB3  LED1
 *   PB1  LED0               PB4  LED2
 *   PB2  SCL (USI)          PB5  RESET — leave as reset
 *
 * All the interesting logic lives in led_core.c and smartiny_slave.c, both of
 * which are pure C and unit-tested on a PC (see ../../tests). This sketch is
 * only the platform glue: transport in, pins out.
 *
 * The .c files are #included directly (a unity build) because the Arduino
 * builder only compiles sources inside the sketch folder. Alternative: copy or
 * symlink them into a src/ subdirectory here.
 */
#include <TinyWireS.h>
#include <EEPROM.h>

#include "../core/led_core.h"
#include "../../../../lib/smartiny-slave/smartiny_slave.h"
#include "../core/led_core.c"
#include "../../../../lib/smartiny-slave/smartiny_slave.c"

#define FW_MAJOR       0
#define FW_MINOR       1
#define LED_COUNT      3        /* PB1, PB3, PB4 — see the 4th-LED note below */
#define EE_SLOT_ADDR   0        /* EEPROM byte holding the I2C address        */
#define I2C_RX_MAX     8

static smartiny_slave_t slave;
static led_core_t       leds;

/* ---- pins ---------------------------------------------------------------
 * Write LED bits with single-bit operations ONLY. On AVR, `PORTB |= (1<<n)`
 * compiles to SBI, a single atomic instruction. A read-modify-write of the
 * whole port would race the USI interrupt, which also manipulates PORTB for
 * SDA/SCL, and could corrupt an in-flight I2C bit.
 */
static inline void leds_drive(uint8_t m)
{
    if (m & 0x01) PORTB |=  (1 << PB1); else PORTB &= ~(1 << PB1);
    if (m & 0x02) PORTB |=  (1 << PB3); else PORTB &= ~(1 << PB3);
    if (m & 0x04) PORTB |=  (1 << PB4); else PORTB &= ~(1 << PB4);
}

/* ---- address persistence ------------------------------------------------ */
static uint8_t addr_load(void)
{
    uint8_t a = EEPROM.read(EE_SLOT_ADDR);
    /* A blank cell reads 0xFF, which is outside the legal range, so an
     * unprogrammed board falls back to the catalogue default. */
    return SMARTINY_ADDR_IS_VALID(a) ? a : SMARTINY_ADDR_LED;
}

/* ---- I2C callbacks — keep these SHORT ----------------------------------- */
static void on_receive(uint8_t howMany)
{
    uint8_t buf[I2C_RX_MAX];
    uint8_t n = 0;
    (void)howMany;
    while (TinyWireS.available() && n < I2C_RX_MAX)
        buf[n++] = TinyWireS.receive();
    smartiny_slave_on_write(&slave, buf, n);
}

static void on_request(void)
{
    TinyWireS.send(smartiny_slave_on_read(&slave));
}

void setup(void)
{
    /* Outputs first, before the USI takes over PB0/PB2. */
    DDRB |= (1 << PB1) | (1 << PB3) | (1 << PB4);
    leds_drive(0);

    led_core_init(&leds, LED_COUNT);
    smartiny_slave_init(&slave,
                        SMARTINY_ID_LED,
                        SMARTINY_VERSION_PACK(FW_MAJOR, FW_MINOR),
                        addr_load(),
                        led_core_read, led_core_write, &leds);

    TinyWireS.begin(slave.i2c_addr);
    TinyWireS.onReceive(on_receive);
    TinyWireS.onRequest(on_request);

    /* Power-on sign of life: each LED in turn, so a dead board is obvious. */
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        leds_drive(1 << i);
        delay(120);
    }
    leds_drive(0);
}

void loop(void)
{
    TinyWireS_stop_check();

    /* Soft PWM: one phase step per pass. The loop is deliberately tight — at
     * 8 MHz this gives a few hundred Hz of PWM, well above flicker. Doing this
     * in the main loop rather than a timer ISR is the rule from
     * architecture.md §9.6: USI must stay the only time-critical handler. */
    leds_drive(led_core_tick(&leds));

    /* A host changed our address: persist it, then adopt it. EEPROM.update
     * skips the write (and the wear) when the value already matches. */
    if (slave.addr_dirty) {
        slave.addr_dirty = 0;
        EEPROM.update(EE_SLOT_ADDR, slave.i2c_addr);
        TinyWireS.begin(slave.i2c_addr);
        /* If your TinyWireS build does not re-begin cleanly, power-cycle the
         * module — the new address is already saved. */
    }
}
