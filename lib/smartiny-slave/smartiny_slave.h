/*
 * smartiny_slave.h — the I2C slave register engine shared by every module.
 *
 * Implements the common header (0x00-0x0F) once, and delegates anything at or
 * above SMARTINY_REG_MODULE_BASE to the module's own read/write callbacks.
 *
 * Deliberately free of any hardware dependency: it is fed bytes by whatever
 * transport the platform uses (TinyWireS on an ATtiny85, hardware TWI on a
 * tinyAVR, a test harness on a PC) so the protocol logic can be unit-tested on
 * a laptop. See docs/architecture.md §4.
 *
 * Transaction model (standard I2C register device):
 *   write [reg]                  -> set the register pointer
 *   write [reg][v0][v1]...       -> set pointer, then write v0, v1, ... with
 *                                   the pointer auto-incrementing
 *   read  -> returns the byte at the pointer, then auto-increments
 */
#ifndef SMARTINY_SLAVE_H
#define SMARTINY_SLAVE_H

#include <stdint.h>
#include "../smartiny-common/smartiny_regs.h"

/* I2C reserves 0x00-0x07 and 0x78-0x7F, so a module may only live in between.
 * Writing anything else to SMARTINY_REG_I2C_ADDR is rejected — otherwise a bad
 * write would strand the module at an unreachable address. */
#define SMARTINY_ADDR_MIN  0x08
#define SMARTINY_ADDR_MAX  0x77
#define SMARTINY_ADDR_IS_VALID(a) ((a) >= SMARTINY_ADDR_MIN && (a) <= SMARTINY_ADDR_MAX)

typedef uint8_t (*smartiny_read_fn)(uint8_t reg, void *ctx);
typedef void    (*smartiny_write_fn)(uint8_t reg, uint8_t val, void *ctx);

typedef struct {
    uint8_t who_am_i;     /* device type id, read-only on the bus            */
    uint8_t version;      /* packed major.minor, read-only                   */
    uint8_t status;       /* module raises bits; read-only on the bus        */
    uint8_t config;       /* host-writable generic config                    */
    uint8_t i2c_addr;     /* current 7-bit address                           */
    uint8_t ptr;          /* register pointer                                */
    uint8_t addr_dirty;   /* set when i2c_addr changed -> platform persists  */

    smartiny_read_fn  mod_read;
    smartiny_write_fn mod_write;
    void             *ctx;
} smartiny_slave_t;

void smartiny_slave_init(smartiny_slave_t *s,
                         uint8_t who_am_i, uint8_t version, uint8_t i2c_addr,
                         smartiny_read_fn rd, smartiny_write_fn wr, void *ctx);

/* Feed a complete master->slave write. buf[0] is always the register pointer. */
void smartiny_slave_on_write(smartiny_slave_t *s, const uint8_t *buf, uint8_t n);

/* Produce the next slave->master byte. */
uint8_t smartiny_slave_on_read(smartiny_slave_t *s);

#endif /* SMARTINY_SLAVE_H */
