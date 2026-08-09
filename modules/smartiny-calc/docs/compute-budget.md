# Can a '85 do the maths? — measured

Short answer: **speed is a non-issue; flash is the whole constraint** — and the
biggest single cost is not what you would guess.

All figures below are **measured**, `avr-gcc 7.3 -Os -mmcu=attiny85`, unless
marked *(est.)*.

## Speed: not close to a problem

At 8 MHz a software float multiply is ~10 µs and even `powf()` lands near a
millisecond. An RPN calculator updates when you press a key, so nothing here is
remotely perceptible. **Do not spend a single decision on execution speed.**

## Flash: the actual budget

Cumulative cost of the Layout C function set:

| Build | Flash | Δ |
|---|---:|---:|
| float `+ − × ÷` only | 1086 B | — |
| + `sinf/cosf/tanf` | 1550 B | +464 |
| + `logf/log10f/powf/sqrtf` | 2312 B | +762 |
| all of the above together | 2802 B | |
| **+ `dtostrf` (float → display string)** | **4372 B** | **+1570** |

> **The formatter costs more than every trig and log function combined.**
> `dtostrf` alone is 1570 bytes — 19 % of the ATtiny85's entire flash — to turn a
> float into text. It is the single most surprising line in this table and the
> first thing to replace.

### Whole-system estimate

| Component | Flash |
|---|---:|
| math + formatting (measured) | 4372 B |
| u8x8 display driver *(est.)* | ~1800 B |
| USI I²C master *(est.)* | ~700 B |
| RPN stack, keymap, main loop *(est.)* | ~700 B |
| **total** | **~7600 / 8192 B — 93 %** |

It **fits**, but with ~600 bytes spare. That is enough for the calculator and
**nothing** for the programmable-RPN work in roadmap item 3.

## The way out: compute it yourself

Replacing libm and `dtostrf` with fixed-point CORDIC and a hand-rolled formatter:

| Approach | Flash |
|---|---:|
| float libm + `dtostrf` | 4372 B |
| **fixed-point CORDIC + own formatter** | **1818 B** |

**2554 bytes saved — 31 % of total flash**, which turns "no room for a program VM"
into ~3 KB free.

CORDIC computes sin, cos, tan, atan, log, exp and sqrt with **only shifts, adds
and a small constant table** — no multiplier, no float library. The whole rotation
kernel is a dozen lines:

```c
static void cordic(int32_t z, int32_t *s, int32_t *c){
  int32_t x=39797, y=0;                       /* 1/K in Q16 */
  for(uint8_t i=0;i<16;i++){
    int32_t xt=x, yt=y;
    if(z>=0){ x=xt-(yt>>i); y=yt+(xt>>i); z-=atan_tab[i]; }
    else    { x=xt+(yt>>i); y=yt-(xt>>i); z+=atan_tab[i]; }
  }
  *c=x; *s=y;
}
```

> This is not a clever workaround — it is **how calculators were actually built**.
> The HP-35 (1972) delivered full scientific functions from roughly 3 KB of ROM
> using CORDIC, because floating-point libraries were not an option. Doing the same
> on an 8 KB part is the project's guiding priority (architecture §1) in its most
> literal form: the constraint is the interesting part.

## ⚠️ The demo's number format is not good enough

The 1818 B measurement uses **Q16.16**, which ranges only ±32768 with ~5
significant digits. Fine for a proof, **wrong for a calculator** — you could not
enter 1×10⁶.

**✅ Since resolved — BCD mantissa + decimal exponent**, measured in
[`number-format.md`](number-format.md). The surprise there is that BCD is not the
expensive option: `+ − × ÷` plus a display formatter costs **1968 B in BCD
against 2456 B in float + `dtostrf`**. Float wins the arithmetic by 654 B and
loses the formatter by 1142 B — because with decimal storage the digits *are*
digits, so binary→decimal conversion is structurally absent rather than
optimised. It also makes `0.1 + 0.2` come out exactly `0.3`.

The 2554 B figure above therefore stands as an **upper bound measured against the
wrong format in both directions** — Q16.16 has too little range to ship and too
little precision for CORDIC, whose outputs live in [−1, 1]. Q2.30 is the right
binary format if a binary kernel is used at all.

## Conclusions

1. **Speed never enters the decision.** Not for any operation in Layout C.
2. **`dtostrf` is the first thing to replace** — 1570 B for number formatting is
   the worst value in the build.
3. **libm is affordable for the calculator alone**, at ~93 % flash. It is *not*
   affordable once the program VM arrives.
4. **CORDIC is the answer, and it is the historically correct one.** Write the
   maths and the formatter, and the '85 has room to spare.
5. **✅ Decided: stay on the '85 and write the maths.** This measurement was the
   strongest ATtiny1624 argument the project has produced — and it was heard and
   declined, because CORDIC dissolves it. The escape hatch is reserved for
   *cannot*; this was a *would be easier*. See
   [`docs/research/chip-strategy.md`](../../../docs/research/chip-strategy.md)
   and architecture §11 decision 5.

## Next: `core/cordic.c`, host-tested

The kernel is pure integer maths — no ADC, no I²C, no AVR — so it belongs in
`core/` and is testable exactly the way `led_core` is: `make test` on a laptop,
`-Wall -Wextra -Werror`, no toolchain required.

That makes the accuracy question tractable rather than nerve-wracking:

| Test | Oracle |
|---|---|
| `sin`/`cos`/`tan` across the full input range | host `double` libm, assert error < 1 ULP of the chosen format |
| `log`/`exp`/`sqrt` likewise | same |
| identities (`sin²+cos²=1`, `exp(log x)=x`) | self-checking, no oracle needed |
| the formatter | round-trip: format → parse → compare |

Sixteen CORDIC iterations converge to ~16 bits, so the iteration count is a
tunable: fewer iterations, less flash and less accuracy, and the test suite tells
you exactly what you bought.

The number format is now settled (BCD+exponent), so the remaining open question
is the **bridge**: a decimal CORDIC that never leaves BCD, or a binary Q2.30
kernel plus conversion routines. That is architecture §11 decision 11, and the
decisive number — what decimal CORDIC actually costs in flash — is unmeasured.
Build `core/num.c` first regardless; both bridges sit on top of it.
