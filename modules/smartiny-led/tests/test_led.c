/*
 * Host-side unit tests for the shared slave engine and the smartiny-led core.
 * Builds and runs on a laptop with no AVR toolchain — `make` in this directory.
 */
#include <stdio.h>
#include <string.h>
#include "../../../lib/smartiny-slave/smartiny_slave.h"
#include "../firmware/core/led_core.h"

static int checks = 0, failures = 0;

#define CHECK(cond, msg) do {                                            \
    checks++;                                                            \
    if (!(cond)) { failures++; printf("  FAIL: %s (%s:%d)\n",            \
                                      msg, __FILE__, __LINE__); }        \
} while (0)

#define CHECK_EQ(got, want, msg) do {                                    \
    checks++;                                                            \
    unsigned _g = (unsigned)(got), _w = (unsigned)(want);                \
    if (_g != _w) { failures++;                                          \
        printf("  FAIL: %s — got 0x%02X want 0x%02X (%s:%d)\n",          \
               msg, _g, _w, __FILE__, __LINE__); }                       \
} while (0)

/* --- helpers: drive the slave the way a real master would ---------------- */
static void wr(smartiny_slave_t *s, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    smartiny_slave_on_write(s, buf, 2);
}

static uint8_t rd(smartiny_slave_t *s, uint8_t reg)
{
    smartiny_slave_on_write(s, &reg, 1);      /* set pointer */
    return smartiny_slave_on_read(s);
}

static void setup(smartiny_slave_t *s, led_core_t *l, uint8_t count)
{
    led_core_init(l, count);
    smartiny_slave_init(s, SMARTINY_ID_LED, SMARTINY_VERSION_PACK(0, 1),
                        SMARTINY_ADDR_LED, led_core_read, led_core_write, l);
}

/* --- common header ------------------------------------------------------- */
static void test_common_header(void)
{
    smartiny_slave_t s; led_core_t l;
    setup(&s, &l, 3);
    printf("common header\n");

    CHECK_EQ(rd(&s, SMARTINY_REG_WHO_AM_I), SMARTINY_ID_LED, "WHO_AM_I");
    CHECK_EQ(rd(&s, SMARTINY_REG_VERSION), SMARTINY_VERSION_PACK(0, 1), "VERSION");
    CHECK(SMARTINY_ID_IS_VALID(rd(&s, SMARTINY_REG_WHO_AM_I)), "id is valid");

    /* identity registers must be immune to host writes */
    wr(&s, SMARTINY_REG_WHO_AM_I, 0x99);
    CHECK_EQ(rd(&s, SMARTINY_REG_WHO_AM_I), SMARTINY_ID_LED, "WHO_AM_I read-only");
    wr(&s, SMARTINY_REG_VERSION, 0x99);
    CHECK_EQ(rd(&s, SMARTINY_REG_VERSION), SMARTINY_VERSION_PACK(0, 1), "VERSION read-only");

    wr(&s, SMARTINY_REG_CONFIG, 0x5A);
    CHECK_EQ(rd(&s, SMARTINY_REG_CONFIG), 0x5A, "CONFIG writable");
}

/* --- address handling: the register that can strand a module ------------- */
static void test_i2c_addr(void)
{
    smartiny_slave_t s; led_core_t l;
    setup(&s, &l, 3);
    printf("i2c address\n");

    CHECK_EQ(rd(&s, SMARTINY_REG_I2C_ADDR), SMARTINY_ADDR_LED, "default addr");
    CHECK_EQ(s.addr_dirty, 0, "not dirty at rest");

    wr(&s, SMARTINY_REG_I2C_ADDR, 0x31);
    CHECK_EQ(rd(&s, SMARTINY_REG_I2C_ADDR), 0x31, "addr accepted");
    CHECK_EQ(s.addr_dirty, 1, "dirty flag set for persistence");

    /* reserved ranges must be rejected, or the module becomes unreachable */
    s.addr_dirty = 0;
    wr(&s, SMARTINY_REG_I2C_ADDR, 0x00);
    CHECK_EQ(rd(&s, SMARTINY_REG_I2C_ADDR), 0x31, "reserved-low rejected");
    wr(&s, SMARTINY_REG_I2C_ADDR, 0x7F);
    CHECK_EQ(rd(&s, SMARTINY_REG_I2C_ADDR), 0x31, "reserved-high rejected");
    wr(&s, SMARTINY_REG_I2C_ADDR, 0x78);
    CHECK_EQ(rd(&s, SMARTINY_REG_I2C_ADDR), 0x31, "0x78 rejected");
    CHECK_EQ(s.addr_dirty, 0, "no spurious persist on rejection");

    CHECK_EQ(SMARTINY_ADDR_IS_VALID(0x08), 1, "0x08 is the low bound");
    CHECK_EQ(SMARTINY_ADDR_IS_VALID(0x77), 1, "0x77 is the high bound");
}

/* --- pointer auto-increment (block reads/writes) ------------------------- */
static void test_autoincrement(void)
{
    smartiny_slave_t s; led_core_t l;
    setup(&s, &l, 3);
    printf("pointer auto-increment\n");

    /* block write: STATE then BRIGHTNESS in one transaction */
    uint8_t blk[3] = { SMARTINY_LED_REG_STATE, 0x05, 0x40 };
    smartiny_slave_on_write(&s, blk, 3);
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_STATE), 0x05, "block write byte 0");
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_BRIGHTNESS), 0x40, "block write byte 1");

    /* block read: COUNT, STATE, BRIGHTNESS back-to-back */
    uint8_t ptr = SMARTINY_LED_REG_COUNT;
    smartiny_slave_on_write(&s, &ptr, 1);
    CHECK_EQ(smartiny_slave_on_read(&s), 3,    "block read COUNT");
    CHECK_EQ(smartiny_slave_on_read(&s), 0x05, "block read STATE");
    CHECK_EQ(smartiny_slave_on_read(&s), 0x40, "block read BRIGHTNESS");

    /* a zero-length write must not corrupt the pointer */
    s.ptr = SMARTINY_LED_REG_STATE;
    smartiny_slave_on_write(&s, blk, 0);
    CHECK_EQ(s.ptr, SMARTINY_LED_REG_STATE, "empty write is a no-op");
}

/* --- LED registers ------------------------------------------------------- */
static void test_led_regs(void)
{
    smartiny_slave_t s; led_core_t l;
    setup(&s, &l, 3);
    printf("led registers\n");

    CHECK_EQ(rd(&s, SMARTINY_LED_REG_COUNT), 3, "COUNT reports population");
    wr(&s, SMARTINY_LED_REG_COUNT, 8);
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_COUNT), 3, "COUNT read-only");

    wr(&s, SMARTINY_LED_REG_STATE, 0x05);
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_STATE), 0x05, "STATE round-trips");

    /* bits above count must not stick — a host must never read back an LED
     * that isn't fitted as if it were lit */
    wr(&s, SMARTINY_LED_REG_STATE, 0xFF);
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_STATE), 0x07, "STATE masked to populated");

    wr(&s, SMARTINY_LED_REG_BRIGHTNESS, 0x80);
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_BRIGHTNESS), 0x80, "BRIGHTNESS round-trips");

    wr(&s, SMARTINY_LED_REG_BLINK, 0x02);
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_BLINK), 0x02, "BLINK round-trips");
    wr(&s, SMARTINY_LED_REG_BLINK, 0xFF);
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_BLINK), 0x07, "BLINK masked to populated");
    wr(&s, SMARTINY_LED_REG_BLINK_MS, 50);
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_BLINK_MS), 50, "BLINK_MS round-trips");
    wr(&s, SMARTINY_LED_REG_BLINK_DUTY, 0x40);
    CHECK_EQ(rd(&s, SMARTINY_LED_REG_BLINK_DUTY), 0x40, "BLINK_DUTY round-trips");
}

/* --- blink gating -------------------------------------------------------- */
static void test_blink(void)
{
    led_core_t l;
    printf("blink\n");

    /* LED0 steady, LED1 blinking, 500 ms period at 50% duty */
    led_core_init(&l, 3);
    l.state = 0x03; l.blink = 0x02; l.blink_ms10 = 50; l.blink_duty = 0x80;

    l.phase = 0;
    CHECK_EQ(led_core_tick(&l, 0)   & 0x03, 0x03, "t=0ms: both lit");
    l.phase = 0;
    CHECK_EQ(led_core_tick(&l, 100) & 0x03, 0x03, "t=100ms: still in on-phase");
    l.phase = 0;
    CHECK_EQ(led_core_tick(&l, 300) & 0x03, 0x01, "t=300ms: blinker off, steady stays");
    l.phase = 0;
    CHECK_EQ(led_core_tick(&l, 600) & 0x03, 0x03, "t=600ms: next period, on again");

    /* a steady LED must never be gated */
    led_core_init(&l, 3);
    l.state = 0x01; l.blink = 0x00; l.blink_ms10 = 50; l.blink_duty = 0x80;
    uint8_t seen_off = 0;
    for (uint16_t t = 0; t < 1000; t += 25) {
        l.phase = 0;
        if ((led_core_tick(&l, t) & 0x01) == 0) seen_off = 1;
    }
    CHECK_EQ(seen_off, 0, "unblinked LED never gated");

    /* period 0 disables gating rather than blacking out */
    led_core_init(&l, 3);
    l.state = 0x02; l.blink = 0x02; l.blink_ms10 = 0;
    l.phase = 0;
    CHECK_EQ(led_core_tick(&l, 777) & 0x02, 0x02, "BLINK_MS=0 means no gating");

    /* duty controls the on-fraction */
    led_core_init(&l, 3);
    l.state = 0x01; l.blink = 0x01; l.blink_ms10 = 100; l.blink_duty = 64; /* 25% */
    int on = 0;
    for (uint16_t t = 0; t < 1000; t += 10) {
        l.phase = 0;
        if (led_core_tick(&l, t) & 0x01) on++;
    }
    CHECK(on >= 20 && on <= 30, "duty 64/255 gives roughly 25% on-time");
}

/* --- soft PWM ------------------------------------------------------------ */
static void test_pwm(void)
{
    led_core_t l;
    printf("soft pwm\n");

    /* brightness 0 must be genuinely dark, not 1/256 lit */
    led_core_init(&l, 3);
    l.state = 0x07; l.brightness = 0; l.phase = 0;
    int lit = 0;
    for (int i = 0; i < 256; i++) if (led_core_tick(&l, 0)) lit++;
    CHECK_EQ(lit, 0, "brightness 0 is fully off");

    /* full brightness: on for 255 of 256 phases */
    led_core_init(&l, 3);
    l.state = 0x07; l.brightness = 0xFF; l.phase = 0;
    lit = 0;
    for (int i = 0; i < 256; i++) if (led_core_tick(&l, 0)) lit++;
    CHECK_EQ(lit, 255, "brightness 255 is effectively full on");

    /* half brightness ~ 50% duty */
    led_core_init(&l, 3);
    l.state = 0x07; l.brightness = 0x80; l.phase = 0;
    lit = 0;
    for (int i = 0; i < 256; i++) if (led_core_tick(&l, 0)) lit++;
    CHECK_EQ(lit, 128, "brightness 128 is 50% duty");

    /* PWM must never light an LED that is off in STATE */
    led_core_init(&l, 3);
    l.state = 0x01; l.brightness = 0xFF; l.phase = 0;
    uint8_t seen = 0;
    for (int i = 0; i < 256; i++) seen |= led_core_tick(&l, 0);
    CHECK_EQ(seen, 0x01, "pwm never lights an off LED");
}

int main(void)
{
    printf("smartiny-led host tests\n\n");
    test_common_header();
    test_i2c_addr();
    test_autoincrement();
    test_led_regs();
    test_blink();
    test_pwm();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
