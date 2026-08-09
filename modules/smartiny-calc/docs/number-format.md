# Number format — measured, and why it is not a staging decision

The question this answers: *prototype in Q16.16, then migrate to BCD+exponent
like HP?*

**No — and the reason is the interesting part.** That plan assumes BCD is the
expensive, proper option you graduate to. It is not. For the four basic
functions plus a display, **BCD is the cheaper option outright.**

All figures measured, `avr-gcc 7.3 -Os -mmcu=attiny85`, matched harnesses,
reproducible with [`tools/flash-bench`](../tools/flash-bench) (`make sizes`).

## The measurement

| Cumulative build | BCD + exp | float + `dtostrf` |
|---|---:|---:|
| baseline harness | 136 | 112 |
| **+ display formatter** | 552 (**+416**) | 1670 (**+1558**) |
| + add / subtract | 1238 | 2094 |
| + multiply | 1618 | 2334 |
| + divide | 2104 | 2568 |
| **net of baseline** | **1968 B** | **2456 B** |

Read it as two opposing columns rather than one total:

| | BCD | float | winner |
|---|---:|---:|---|
| arithmetic | 1552 B | 898 B | **float, by 654 B** |
| formatting | 416 B | 1558 B | **BCD, by 1142 B** |
| **net** | **1968 B** | **2456 B** | **BCD, by 488 B** |

Float genuinely wins the arithmetic — a hardware-shaped binary format with a
compiler-supplied library should. It then loses two and a half times that
advantage back at the display.

## Why the formatter is where binary loses

`dtostrf` costs 1558 bytes because **binary → decimal conversion is a real
algorithm**, not a formatting convenience. Turning a float into digits means
repeated division, carrying rounding decisions across digit positions, and
deciding where the value stops being significant.

With a BCD mantissa the digits **are already digits**. The entire formatter is
`'0' + nibble`, plus decimal-point placement from the exponent:

```c
*p++ = (char)('0' + dget(n, i));
```

That is why the cost does not shrink — it is **structurally absent**. You cannot
optimise `dtostrf` down to 416 B; you can only stop needing it. And this is the
single biggest line in the whole flash budget, so where it goes decides the
budget.

> **Corollary that shapes the CORDIC plan:** a hybrid — BCD storage with a
> *binary* CORDIC kernel — reintroduces exactly the binary↔decimal conversion
> that going BCD deleted, on every transcendental call. That is a real argument
> for keeping the transcendentals decimal too. See below.

## The reason that is not about flash at all

```
  ok   0.1+0.2        0.3
```

A binary format cannot represent 0.1, so `0.1 + 0.2` lands a hair off 0.3.
Hidden by rounding to 6 displayed digits — until it isn't, and the machine shows
`0.30000000000000004` or insists 2.675 rounds to 2.67.

**People test calculators with decimal fractions.** It is the first thing anyone
types. HP went BCD in 1972 for exactly this reason, and it is why an HP-35 feels
trustworthy in a way a naive float calculator does not. Storing decimal means
every digit entered is stored as entered — entry, arithmetic and display all
agree, with no representable-value gap anywhere in the chain.

Getting that *and* saving 488 bytes is the rare case where the honest engineering
and the cheap engineering point the same way.

## The format

HP-35 shaped: 10-digit mantissa, decimal exponent.

```c
typedef struct {
    uint8_t m[5];    /* 10 BCD digits packed 2/byte; d0 is the MSD */
    int8_t  exp;     /* value = d0.d1d2...d9 x 10^exp */
    uint8_t flags;   /* sign, and later NaN / overflow */
} num_t;             /* 7 bytes */
```

**7 bytes per number.** The RPN stack `X Y Z T` plus `LASTx` is 35 bytes of the
'85's 512 — the SRAM objection to BCD does not survive contact with arithmetic.

Range is ±9.999999999×10⁹⁹, which is HP's range and comfortably more than
Q16.16's ±32768. The thing that killed Q16.16 as a product format — you could not
enter 1×10⁶ — is simply gone.

## Why Q16.16 is the wrong binary format even for a prototype

CORDIC rotates by angles below 1.74 rad and produces results in [−1, 1]. **Q16.16
spends sixteen bits on integer range the algorithm never uses.** Q2.30 puts those
bits into the fraction instead: ~30 fractional bits against 16, four more decimal
digits, same 32-bit word, same instruction count.

So the earlier 1818 B CORDIC figure was measured against a format that was
suboptimal in *both* directions — too little range to ship, too little precision
for the maths. **Q2.30 is the format for a binary kernel.**

## Recommended staging

The scaffold is real, but it belongs in the test suite, never in the product.

1. **`core/num.c` first — BCD+exponent.** Entry, normalise, format, `+ − × ÷`,
   compare. Measured at 1968 B, host-tested, and it is what every other decision
   sits on. Doing this first means entry and display code get written **once**.
2. **`cordic_bin.c` in Q2.30 — a scaffold, host-only.** Learn the rotation, the
   gain constant `K`, domain reduction, and the iteration/accuracy dial against
   `double` libm. Its durable asset is **the accuracy harness and the
   understanding, not the kernel code.**
3. **Then bridge, and measure before committing:**

| Option | Cost | Note |
|---|---|---|
| **decimal CORDIC** (`atan(10⁻ⁱ)` table, pseudo-multiplication) | ⚠️ **unmeasured** | HP-faithful; no conversion code at all; the "shift" is a nibble shuffle, which is cheap on BCD; slower — up to 9 add/subtracts per digit instead of 1 per bit |
| BCD ↔ Q2.30 conversion + the step-2 kernel | less new code | reintroduces the binary↔decimal conversion BCD was chosen to delete |

Decimal CORDIC is the historically faithful path and avoids the conversion, but
**its flash cost is the one number in this document I have not measured** — and
guessing it is exactly the habit this file exists to avoid. Measure it before
choosing.

## What the bench does *not* establish

- **`num.c` is a cost probe with 15 passing tests, not finished firmware.** No
  divide-by-zero handling, no overflow/underflow flags, no rounding mode — it
  truncates where HP rounded. Each of those adds bytes.
- **Division carries one guard digit**, enough for the tested cases. HP carried
  more; transcendentals will want them, because CORDIC accumulates error across
  iterations and the last digit is where it shows.
- **Nothing here is timed.** BCD arithmetic is slower than float — schoolbook
  multiply is 100 digit-products, and division is repeated subtraction. It does
  not matter (see [`compute-budget.md`](compute-budget.md): the display updates
  on a keypress), but it is not measured, so it should not be claimed.
