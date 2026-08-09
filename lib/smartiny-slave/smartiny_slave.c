#include "smartiny_slave.h"

void smartiny_slave_init(smartiny_slave_t *s,
                         uint8_t who_am_i, uint8_t version, uint8_t i2c_addr,
                         smartiny_read_fn rd, smartiny_write_fn wr, void *ctx)
{
    s->who_am_i   = who_am_i;
    s->version    = version;
    s->status     = 0;
    s->config     = 0;
    s->i2c_addr   = SMARTINY_ADDR_IS_VALID(i2c_addr) ? i2c_addr : SMARTINY_ADDR_MIN;
    s->ptr        = 0;
    s->addr_dirty = 0;
    s->mod_read   = rd;
    s->mod_write  = wr;
    s->ctx        = ctx;
}

static uint8_t read_reg(smartiny_slave_t *s, uint8_t reg)
{
    switch (reg) {
    case SMARTINY_REG_WHO_AM_I: return s->who_am_i;
    case SMARTINY_REG_VERSION:  return s->version;
    case SMARTINY_REG_STATUS:   return s->status;
    case SMARTINY_REG_CONFIG:   return s->config;
    case SMARTINY_REG_I2C_ADDR: return s->i2c_addr;
    default: break;
    }
    if (reg >= SMARTINY_REG_MODULE_BASE && s->mod_read)
        return s->mod_read(reg, s->ctx);
    return 0x00;   /* unmapped register in the common header window */
}

static void write_reg(smartiny_slave_t *s, uint8_t reg, uint8_t val)
{
    switch (reg) {
    /* WHO_AM_I / VERSION / STATUS are read-only: silently ignore writes so a
     * sloppy host cannot corrupt module identity. */
    case SMARTINY_REG_WHO_AM_I:
    case SMARTINY_REG_VERSION:
    case SMARTINY_REG_STATUS:
        return;
    case SMARTINY_REG_CONFIG:
        s->config = val;
        return;
    case SMARTINY_REG_I2C_ADDR:
        if (SMARTINY_ADDR_IS_VALID(val) && val != s->i2c_addr) {
            s->i2c_addr   = val;
            s->addr_dirty = 1;   /* platform persists to EEPROM, then re-begins */
        }
        return;
    default: break;
    }
    if (reg >= SMARTINY_REG_MODULE_BASE && s->mod_write)
        s->mod_write(reg, val, s->ctx);
}

void smartiny_slave_on_write(smartiny_slave_t *s, const uint8_t *buf, uint8_t n)
{
    if (n == 0)
        return;

    s->ptr = buf[0];             /* every write starts with the register pointer */

    for (uint8_t i = 1; i < n; i++) {
        write_reg(s, s->ptr, buf[i]);
        s->ptr++;                /* auto-increment for block writes */
    }
}

uint8_t smartiny_slave_on_read(smartiny_slave_t *s)
{
    uint8_t v = read_reg(s, s->ptr);
    s->ptr++;                    /* auto-increment for block reads */
    return v;
}
