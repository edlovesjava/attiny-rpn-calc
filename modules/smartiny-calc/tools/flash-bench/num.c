/* BCD mantissa + decimal exponent, HP-35 shaped: 10 digits + exp.
 * Measurement build: how much flash does a decimal number system cost on an '85?
 */
#include <stdint.h>

#define NDIG 10

typedef struct {
    uint8_t m[NDIG / 2];   /* packed BCD, m[0] = digits 0(hi),1(lo); d0 is MSD */
    int8_t  exp;           /* value = d0.d1d2...d9 x 10^exp */
    uint8_t flags;
} num_t;

#define NUM_NEG  0x01u

static uint8_t dget(const num_t *n, uint8_t i)
{
    uint8_t b = n->m[i >> 1];
    return (i & 1u) ? (uint8_t)(b & 0x0Fu) : (uint8_t)(b >> 4);
}

static void dset(num_t *n, uint8_t i, uint8_t v)
{
    uint8_t *p = &n->m[i >> 1];
    if (i & 1u) *p = (uint8_t)((*p & 0xF0u) | v);
    else        *p = (uint8_t)((*p & 0x0Fu) | (uint8_t)(v << 4));
}

/* ---------------- formatting: the whole point of storing decimal ---------- */
#if STAGE >= 1
uint8_t num_format(const num_t *n, char *out)
{
    char *p = out;
    uint8_t i, last;
    int8_t e = n->exp;

    if (n->flags & NUM_NEG) *p++ = '-';

    last = NDIG - 1;
    while (last && dget(n, last) == 0) last--;

    if (e < -4 || e > 9) {                       /* scientific */
        *p++ = (char)('0' + dget(n, 0));
        if (last) {
            *p++ = '.';
            for (i = 1; i <= last; i++) *p++ = (char)('0' + dget(n, i));
        }
        *p++ = 'E';
        if (e < 0) { *p++ = '-'; e = (int8_t)-e; } else *p++ = '+';
        *p++ = (char)('0' + (uint8_t)e / 10);
        *p++ = (char)('0' + (uint8_t)e % 10);
    } else if (e >= 0) {                         /* fixed, >= 1 */
        for (i = 0; i <= (uint8_t)e; i++)
            *p++ = (char)('0' + (i <= last ? dget(n, i) : 0));
        if (last > (uint8_t)e) {
            *p++ = '.';
            for (i = (uint8_t)e + 1; i <= last; i++) *p++ = (char)('0' + dget(n, i));
        }
    } else {                                     /* fixed, 0.000ddd */
        *p++ = '0'; *p++ = '.';
        for (i = 0; i < (uint8_t)(-e) - 1u; i++) *p++ = '0';
        for (i = 0; i <= last; i++) *p++ = (char)('0' + dget(n, i));
    }
    *p = '\0';
    return (uint8_t)(p - out);
}
#endif

/* ---------------- magnitude primitives ----------------------------------- */
#if STAGE >= 2
static uint8_t mag_add(num_t *a, const num_t *b)
{
    uint8_t i = NDIG, carry = 0;
    while (i-- > 0) {
        uint8_t s = (uint8_t)(dget(a, i) + dget(b, i) + carry);
        if (s >= 10u) { s = (uint8_t)(s - 10u); carry = 1; } else carry = 0;
        dset(a, i, s);
    }
    return carry;
}

static uint8_t mag_sub(num_t *a, const num_t *b)
{
    uint8_t i = NDIG, borrow = 0;
    while (i-- > 0) {
        int8_t d = (int8_t)(dget(a, i) - dget(b, i) - borrow);
        if (d < 0) { d = (int8_t)(d + 10); borrow = 1; } else borrow = 0;
        dset(a, i, (uint8_t)d);
    }
    return borrow;
}

static int8_t mag_cmp(const num_t *a, const num_t *b)
{
    uint8_t i;
    for (i = 0; i < NDIG; i++) {
        uint8_t x = dget(a, i), y = dget(b, i);
        if (x != y) return (x > y) ? (int8_t)1 : (int8_t)-1;
    }
    return 0;
}

/* shift mantissa right k digits (toward less significant), zero-fill at top */
static void mag_shr(num_t *a, uint8_t k)
{
    uint8_t i;
    if (k >= NDIG) { for (i = 0; i < NDIG / 2; i++) a->m[i] = 0; return; }
    for (i = NDIG; i-- > k; ) dset(a, i, dget(a, (uint8_t)(i - k)));
    for (i = 0; i < k; i++)   dset(a, i, 0);
}

static void num_norm(num_t *n)
{
    uint8_t i, lead = 0;
    while (lead < NDIG && dget(n, lead) == 0) lead++;
    if (lead == NDIG) { n->exp = 0; n->flags = 0; return; }   /* zero */
    if (lead) {
        for (i = 0; i + lead < NDIG; i++) dset(n, i, dget(n, (uint8_t)(i + lead)));
        for (; i < NDIG; i++) dset(n, i, 0);
        n->exp = (int8_t)(n->exp - (int8_t)lead);
    }
}

void num_add(num_t *r, const num_t *a, const num_t *b)
{
    num_t x = *a, y = *b;
    int8_t d;

    if (x.exp < y.exp) { num_t t = x; x = y; y = t; }
    d = (int8_t)(x.exp - y.exp);
    if (d >= NDIG) { *r = x; return; }
    mag_shr(&y, (uint8_t)d);
    y.exp = x.exp;

    if (((x.flags ^ y.flags) & NUM_NEG) == 0) {          /* like signs: add */
        if (mag_add(&x, &y)) {                           /* carry out the top */
            mag_shr(&x, 1);
            dset(&x, 0, 1);
            x.exp++;
        }
    } else {                                             /* unlike: subtract */
        if (mag_cmp(&x, &y) < 0) { num_t t = x; x = y; y = t; }
        (void)mag_sub(&x, &y);
        num_norm(&x);
    }
    *r = x;
}
#endif

/* ---------------- multiply: schoolbook, 10x10 keeping the top 10 ---------- */
#if STAGE >= 3
void num_mul(num_t *r, const num_t *a, const num_t *b)
{
    uint8_t acc[NDIG * 2];
    uint8_t i, j;
    num_t out;

    for (i = 0; i < NDIG * 2; i++) acc[i] = 0;

    for (i = NDIG; i-- > 0; ) {
        uint8_t bi = dget(b, i);
        uint8_t carry = 0;
        if (!bi) continue;
        for (j = NDIG; j-- > 0; ) {
            uint8_t k = (uint8_t)(i + j + 1);
            uint8_t t = (uint8_t)(acc[k] + (uint8_t)(dget(a, j) * bi) + carry);
            carry = (uint8_t)(t / 10u);
            acc[k] = (uint8_t)(t % 10u);
        }
        acc[i] = (uint8_t)(acc[i] + carry);
    }

    out.exp   = (int8_t)(a->exp + b->exp + 1);
    out.flags = (uint8_t)((a->flags ^ b->flags) & NUM_NEG);
    for (i = 0; i < NDIG; i++) dset(&out, i, acc[i]);
    num_norm(&out);
    *r = out;
}
#endif

/* ---------------- divide: restoring, HP's repeated-subtraction shape ------ */
#if STAGE >= 4
void num_div(num_t *r, const num_t *a, const num_t *b)
{
    num_t rem = *a, div = *b, out;
    uint8_t i, j, hi = 0, adj = 0;  /* hi = guard digit: rem*10 overflows NDIG */

    rem.exp = 0; rem.flags = 0;
    div.exp = 0; div.flags = 0;

    /* Both operands are normalised, so the ratio is in [1/10, 10). If it is
     * below 1 the first quotient digit would be 0 and normalising afterwards
     * would discard a digit -- pre-scale instead, and pay for it in the
     * exponent. This is what a guard digit buys in a real implementation. */
    if (mag_cmp(&rem, &div) < 0) {
        hi = dget(&rem, 0);
        for (j = 0; j + 1 < NDIG; j++) dset(&rem, j, dget(&rem, (uint8_t)(j + 1)));
        dset(&rem, NDIG - 1, 0);
        adj = 1;
    }

    for (i = 0; i < NDIG; i++) {
        uint8_t q = 0;
        while (hi || mag_cmp(&rem, &div) >= 0) {
            if (mag_sub(&rem, &div)) hi--;      /* borrow lands in the guard */
            q++;
        }
        dset(&out, i, q);
        hi = dget(&rem, 0);                     /* rem *= 10 */
        for (j = 0; j + 1 < NDIG; j++) dset(&rem, j, dget(&rem, (uint8_t)(j + 1)));
        dset(&rem, NDIG - 1, 0);
    }
    out.exp   = (int8_t)(a->exp - b->exp - (int8_t)adj);
    out.flags = (uint8_t)((a->flags ^ b->flags) & NUM_NEG);
    num_norm(&out);
    *r = out;
}
#endif

/* ---------------- measurement harness ------------------------------------ */
#ifndef HOST_TEST
volatile uint8_t sink;
volatile num_t   vin_a, vin_b;
char             buf[24];

int main(void)
{
    num_t a = vin_a, b = vin_b, r;
    (void)a; (void)b; (void)r;
#if STAGE >= 1
    sink = num_format(&a, buf);
#endif
#if STAGE >= 2
    num_add(&r, &a, &b); vin_a = r;
#endif
#if STAGE >= 3
    num_mul(&r, &a, &b); vin_b = r;
#endif
#if STAGE >= 4
    num_div(&r, &a, &b); vin_a = r;
#endif
    for (;;) sink++;
}
#endif /* HOST_TEST */
