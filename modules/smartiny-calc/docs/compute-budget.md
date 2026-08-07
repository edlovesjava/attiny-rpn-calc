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

A real implementation needs an exponent. Three honest options, undecided:

| Scheme | Range | Notes |
|---|---|---|
| **BCD mantissa + exponent** | full | what HP did; decimal-exact, no binary rounding surprises; most code to write |
| **float storage + CORDIC transcendentals** | full | keeps the 1086 B float core, drops libm's 1716 B and `dtostrf`; middle path |
| Wider fixed point (Q40.24 etc.) | wide, not huge | simplest, still cannot do scientific notation |

The measured 2554 B saving is therefore an **upper bound**; a scheme with proper
range will give back some of it. The direction holds regardless — the formatter
and the transcendentals are where the flash goes, and both are replaceable.

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
tunable: fewer iterations, less flash and less accuracy, and the test suite
tells you exactly what you bought. **Settle
[§11 decision 10](../../../docs/architecture.md) (the number format) first** —
it fixes the fixed-point scale the whole kernel is written against, and it is
much cheaper to decide than to retrofit.
