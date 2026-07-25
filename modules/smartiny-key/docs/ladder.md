# Keypad Resistor-Ladder — Build Sheet

Concrete values for decoding the passive 4×4 matrix on **one ADC pin** (PB3),
produced by [`tools/ladder_optimizer.py`](../tools/ladder_optimizer.py). See
[`architecture.md`](../../../docs/architecture.md) §6.1 (decode) and §9.6 (wake) for the theory.

## Circuit

```
VCC ──┬─[Rr0]── Row1        SENSE ──┬─[Rc0]── Col1
      ├─[Rr1]── Row2                ├─[Rc1]── Col2
      ├─[Rr2]── Row3                ├─[Rc2]── Col3
      └─[Rr3]── Row4                └─[Rc3]── Col4
                               SENSE ──[Rload]── GND
                               SENSE ──[Csense 10nF]── GND
                               SENSE ─────────────────► ADC (PB3 / ADC3)
```

`keycode = 4·row + col` (0–15). `Rr0 = Rc0 = 0 Ω` are direct wires.
`V_sense/VCC = Rload/(Rr_i + Rc_j + Rload)` — **ratiometric, so the ADC codes are
identical at 3.3 V or 5 V.**

## The design frontier (pick your wake strategy)

Lower wake floor → more ADC range → bigger gaps, but wake must then come from
something other than PB3's digital threshold. Resolution barely moves, so the
unified-wake design wins:

| Wake strategy | Min ADC gap | keycode-15 frac | Notes |
|---|---|---|---|
| Decoupled (comparator / master-poll) | 17 | 0.538 | best resolution; needs a separate wake path |
| PCINT floor 0.60 (VIH spec) | 15 | 0.601 | **only 1-count margin — too tight** |
| **PCINT + margin 0.65 (recommended)** | **14** | **0.662** | one pin wakes *and* decodes; 12-count wake margin |

## ✅ Recommended design — unified PCINT wake (0.65 floor)

| Resistor | Value (E24) | | Resistor | Value (E24) |
|---|---|---|---|---|
| Rr0 (row 1) | 0 Ω (wire) | | Rc0 (col 1) | 0 Ω (wire) |
| Rr1 (row 2) | **5.6 kΩ** | | Rc1 (col 2) | **1.1 kΩ** |
| Rr2 (row 3) | **11 kΩ** | | Rc2 (col 3) | **2.7 kΩ** |
| Rr3 (row 4) | **16 kΩ** | | Rc3 (col 4) | **3.9 kΩ** |
| Rload | **39 kΩ** | | Csense | **10 nF** |

- **Min adjacent ADC gap: 14 counts** (10-bit). Half-gap guard ≈ 7 counts; after
  ~1–2 counts lost to switch contact resistance and sub-LSB filtered noise, real
  margin ≈ 5 counts. Comfortable with N-sample debounce.
- **Wake margin: 12 counts** — keycode 15 sits at frac 0.662, clear of VIH (0.60).
- **Max source impedance: 13.2 kΩ** (at keycode 15) → the 10 nF cap + a slow ADC
  clock (prescale to ~50–125 kHz, or extend sample time) keep sampling accurate.

### Full decode table

```
key row col  Rtot(Ω)   frac   ADC   gap
  0   0   0        0   1.000  1023    28
  1   0   1     1100   0.973   995    38
  2   0   2     2700   0.935   957    27
  3   0   3     3900   0.909   930    35
  4   1   0     5600   0.874   895    22
  5   1   1     6700   0.853   873    30
  6   1   2     8300   0.825   843    20
  7   1   3     9500   0.804   823    25
  8   2   0    11000   0.780   798    17
  9   2   1    12100   0.763   781    24
 10   2   2    13700   0.740   757    17
 11   2   3    14900   0.724   740    15
 12   3   0    16000   0.709   725    14
 13   3   1    17100   0.695   711    20
 14   3   2    18700   0.676   691    14
 15   3   3    19900   0.662   677    —
```

Idle / no-press sits at ADC ≈ 0 (SENSE pulled to GND by Rload), far below
keycode 15.

## Firmware decode

Thresholds are the midpoints between adjacent codes, ordered by keycode:

```c
// ADC >= KEY_THRESH[k]  =>  at least keycode k (codes descend with keycode)
static const uint16_t KEY_THRESH[16] = {
  1009, 976, 943, 912, 884, 858, 833, 810,
   789, 769, 748, 732, 718, 701, 684, 507
};
#define KEY_NONE 0xFF

uint8_t decode_key(uint16_t adc) {
    if (adc < KEY_THRESH[15]) return KEY_NONE;   // idle / no press
    for (uint8_t k = 0; k < 16; k++)             // first (smallest) k that fits
        if (adc >= KEY_THRESH[k]) return k;
    return KEY_NONE;
}
```

Wrap with **N consecutive stable reads** (debounce) before emitting a key event.
Wake: idle SENSE = 0 → any press pulls PB3 above VIH → PCINT3 rising edge wakes
the MCU, which then runs `decode_key()`. The same wire wakes and decodes.

## Bench validation

Before committing the values to a PCB, prove them on a breadboard with any
10-bit AVR (Uno / Nano / Pro Mini) reading `SENSE`. Sketch:
[`tools/ladder_test/ladder_test.ino`](../tools/ladder_test/ladder_test.ino).

### Emulating the matrix

All 16 keycodes are reachable with any rig below. They differ in how easy it is
to *accidentally* produce an invalid reading — prefer the ones where a bad state
is structurally impossible rather than merely discouraged.

**1. One jumper wire — recommended.** A key press *is* a short from `Row_i` to
`Col_j`, so a single wire with one end in a row node and the other in a column
node **is** the press. Move it 16 times.

```
VCC ──[Rr_i]── Row_i ─────┐
                          │  ← one jumper = one press
SENSE ─[Rc_j]── Col_j ────┘     keycode = 4*i + j
```

- One wire can only ever connect **one** row to **one** column, so multi-close —
  and with it the 0 Ω trap below — **cannot happen**.
- No `PRESS` rail, no extra parts, nothing that can be left latched.

**2. Two jumpers to a common node.** Plant the row jumper, sweep the column
jumper across its 4 nodes, then advance the row — a faster sweep than re-seating
both ends. Still exactly one row and one column by construction, so it stays
safe. The shared node must float (see below).

**3. Two 1P4T rotary switches**, commons tied. A rotary switch is **mutually
exclusive by construction** — it physically cannot select two rows — so it is as
safe as a jumper with far better ergonomics. Best option if you have them: one
knob per axis, dial in row and column.

**4. Eight slide switches.** Works, but they latch — read the 0 Ω trap warning
below before trusting a reading.

**5. The real 4×4 keypad module.** Highest fidelity; switch to it as soon as it
arrives. Momentary buttons make multi-close self-correcting.

**For any rig with a common tie (2–4): the shared node must float.** Switches
that pull row/column pins to *ground* — the usual "simulate a digital input" rig
— cannot work: both pins land at GND, the row resistor just dumps VCC into
ground, and `SENSE` stays at 0. Tying the commons **to each other** is what
reproduces the short a real key makes.

Partial presses are safe: row-only or col-only leaves the path dead-ended, so
`SENSE` sits at 0 and reads as idle, exactly as on a real keypad. Closing two
rows (or two cols) puts resistors in parallel and decodes wrong — the same
single-key limitation the sticky-modifier design already assumes.

> ⚠️ **The 0 Ω trap — specific to a *latching* switch rig.** Row1 and Col1 are
> 0 Ω links, and anything in parallel with 0 Ω **is** 0 Ω. So leaving Row1 closed
> while also closing Row3 does **not** read as garbage — it reads as a perfectly
> valid *row 0*, silently. Electrically it is indistinguishable from a clean
> press, so **no firmware check can catch it**; only the deviation-from-centre
> flag catches the other multi-close combinations. Momentary buttons make this
> impossible, but slide switches latch — so make "all switches off between
> readings" a rig habit, and be suspicious of an unexpected row-0/col-0 result.
> **Rigs 1–3 above avoid this entirely**, which is the main reason to prefer them.

### Rig rules

- **No indicator LEDs on the ladder nodes.** A 220 Ω branch to VCC completely
  swamps a 39 kΩ network. Use DPDT switches (one pole for the ladder, one for
  the LED to GND), or drop the LEDs and read the serial output — the raw ADC
  count is the more useful indicator anyway. Driving two LEDs from the MCU's
  *decoded* row/col closes the loop nicely.
- **Power the ladder from the MCU's own VCC.** Required for ratiometric
  operation, and a 5 V `SENSE` into a 3.3 V ADC pin is out of spec.
- **`analogReference(DEFAULT)`** (= AVcc). The internal 1.1 V bandgap breaks the
  scheme entirely.
- **Allow ~1 ms after a key change.** Source impedance peaks at 13.2 kΩ, so
  τ ≈ 132 µs with `Csense`; also discard the first conversion after touching the
  pin so the sample-and-hold settles.
- Keep `Csense` close to the ADC pin.

### Pass criteria

| Check | Expect |
|---|---|
| All 16 keycodes decode, monotonic | key *n* never reads as *n*±1 |
| `err` vs the design centre | within ≈ ±5 counts |
| `margin` to nearest boundary | ≥ ~5 counts on **every** key |
| Idle | ≈ 0, far below `KEY_THRESH[15]` = 507 |

A `margin` consistently squeezed in one direction means `Rload` wants a nudge —
re-run the optimizer. Margin lost to resistor spread is what the EEPROM
self-calibration below is for.

### Step 2 — the full 16-switch matrix on a breadboard

Once the jumper rig proves the levels, build the real topology. Electrically it
is an ordinary 4×4 matrix — the only difference from a scanned keypad is what
hangs off the row and column lines:

```
              COL0      COL1      COL2      COL3
VCC ─[  0Ω]─ ROW0 ─┬─SW0──┬──SW1───┬──SW2───┬──SW3
VCC ─[5.6kΩ]─ ROW1 ─┼─SW4──┼──SW5───┼──SW6───┼──SW7
VCC ─[ 11kΩ]─ ROW2 ─┼─SW8──┼──SW9───┼──SW10──┼──SW11
VCC ─[ 16kΩ]─ ROW3 ─┼─SW12─┼──SW13──┼──SW14──┼──SW15
                    │      │        │        │
                 [  0Ω] [1.1kΩ]  [2.7kΩ]  [3.9kΩ]
                    └──────┴────────┴────────┴──► SENSE ─┬─[39kΩ]─ GND
                                                          └─[10nF]─ GND
```

`SWn` sits at (row `n/4`, col `n%4`) and shorts that row line to that column
line — nothing else. **No diodes**: they would drop voltage into the divider and
destroy the measurement (see multi-press below for what we get instead).

**Breadboard mapping.** 6 mm tactile switches straddle the centre channel, so
each one bridges a top-half strip to a bottom-half strip. Place them in 4 groups
of 4, leaving a spare column between groups:

- **Top half** → jumper each group's 4 strips together = `ROW_i`, then `Rr_i` up
  to the VCC rail. (One group = one row.)
- **Bottom half** → jumper the *same offset* across all 4 groups = `COL_j`
  (4 long buses running the length of the board), then `Rc_j` to `SENSE`.

That is 16 switches, ~24 jumpers, 6 resistors, `Rload` and `Csense`.

> **Tactile-switch gotcha:** the 4 pins are two internally-shorted *pairs*. When
> the switch straddles the channel, both top pins are one terminal and both
> bottom pins are the other — so a switch that seems "always closed" is rotated
> 90°.

> **The matrix is topological, not geometric.** For pure electrical validation
> you may lay the 16 switches in a single line and wire them logically; only the
> finished keypad needs to *look* like a grid. Use the 4×4 physical arrangement
> when you want to test it by feel.

### Step 2b — perfboard keypad module (recommended)

Tactile switches breadboard badly, so build the **matrix** on perfboard and leave
the **ladder** on the breadboard. Keep the boundary exactly where the commercial
module puts it — 16 switches, 8 leads, nothing else:

```
   perfboard (passive matrix)          breadboard (the clever part)
  ┌──────────────────────┐
  │ SW0  SW1  SW2  SW3   │   ROW0 ──┐
  │ SW4  SW5  SW6  SW7   │   ROW1 ──┤  8-pin      ┌─ Rr0..Rr3 ─ VCC
  │ SW8  SW9  SW10 SW11  │   ROW2 ──┤  male   ────┤
  │ SW12 SW13 SW14 SW15  │   ROW3 ──┤  header     ├─ Rc0..Rc3 ─ SENSE
  └──────────────────────┘   COL0-3─┘             └─ Rload, Csense
```

**Why this split is worth the effort**

- **Pin-compatible with the sourced module.** Match the standard pinout and your
  perfboard and the AliExpress keypad become drop-in swaps for each other.
- **The ladder stays swappable.** Resistor values are the thing still being
  tuned; keep them where you can pull one with tweezers.
- **One keypad, three decode strategies.** The same passive board can be driven
  by the 1-ADC ladder ('85), a classic 8-GPIO scan (tinyAVR), or the 2-ADC
  variant — so it stays a useful test fixture no matter which way the platform
  goes.

**Pinout — match the commercial convention**

Single-row 8-pin 0.1" male header, viewed from the front with keys up:

```
   pin  1   2   3   4   5   6   7   8
        R1  R2  R3  R4  C1  C2  C3  C4
```

Cheap modules do vary — **buzz out the real one with a continuity meter** before
trusting it. The physical key legend is only a firmware lookup table, so any
consistent wiring works; matching the standard just makes swapping painless.

**Construction**

- The header plugs **straight into the breadboard**: each of the 8 pins lands in
  its own 5-hole strip, and that strip *is* the row/column node the ladder
  resistors attach to. No flying leads.
- On the back, the two axes must cross without shorting: run **one axis in bare
  tinned bus wire** (rows, soldered straight across the switch pins) and the
  **other in insulated wire** (columns). Bare-on-bare is the classic way to build
  a shorted matrix.
- Each switch's 4 pins are two internally-shorted pairs, so you only need **one
  pin per pair** — use a diagonal pair and leave the other two unsoldered.
- 6 mm tactile switches don't natively land on a 0.1" grid (6.5 × 4.5 mm pitch);
  the legs splay to 3 × 2 holes (7.62 × 5.08 mm) with light persuasion.
  **Test-fit one before committing to a layout.**
- Added contact/solder resistance is well under an ohm — negligible against the
  1.1 kΩ smallest ladder step. Keep `Csense` on the breadboard next to the ADC
  pin, not out at the keypad.

### Multi-press behaviour

Every simultaneous press adds a **parallel** current path, and parallel
resistance is always lower than either branch — so `R_total` always falls and
`SENSE` always rises. That yields one crisp, exhaustively verified rule:

> **A multi-press never decodes above the lowest key pressed.**
> Verified for all 120 two-key combinations: `decoded ≤ min(pressed)`.

| Pressed | Case | R_total | ADC | Decodes as |
|---|---|---|---|---|
| 5 + 6 | same row | `Rr1 + (Rc1∥Rc2)` | 879 | **5** (err +6) |
| 4 + 7 | same row, col0 | `Rr1 + (0∥Rc3)` = `Rr1` | 895 | **4** (err 0) |
| 8 + 12 | same column | `(Rr2∥Rr3) + Rc0` | 877 | **5** (err +4) |
| 5 + 10 | diagonal | `(Rr1+Rc1) ∥ (Rr2+Rc2)` | 917 | **3** (err −13) |
| 0 + anything | absorbing | `0 ∥ x` = 0 | 1023 | **0** (err 0) |

**The deviation check is not enough on its own.** Of the 120 pairs, only **24**
land far enough off-centre to trip `SUSPECT_ERR`; **96 read as a plausible key**,
and **39 are exact aliases** of a key that really is pressed. Analog detection
alone cannot solve this.

**Firmware policy — require a return to idle between keys.** This makes the whole
problem benign without any extra detection:

1. Idle → key A settles → emit `A`, enter *held*.
2. While *held*, ignore every reading change — the combined value from a second
   press is simply never emitted.
3. Only after `SENSE` returns to idle does the next key become emittable.

Press A-then-B and you get exactly `A`; the collision value is discarded. The one
residual gap is a *genuinely simultaneous* press (both inside the debounce
window), which yields a phantom key ≤ min — caught by `SUSPECT_ERR` for 24 pairs
and undetectable for the rest. Rare in practice, and the sticky-modifier design
means chording is never required.

> **Design consequence — keep key 0 harmless.** `Rr0 = Rc0 = 0 Ω` makes keycode 0
> an *absorbing element*: key 0 plus anything reads as exactly key 0. Spurious
> key-0 events are therefore the single most likely collision artifact, so key 0
> should carry a benign, idempotent function — never `CLEAR`, `OFF`, or anything
> destructive.

## BOM (per keypad)

- 1× passive 4×4 matrix keypad module (8 leads, no controller), or 16× **SPST-NO
  momentary** tactile switches. NO and momentary are both required: normally-closed
  keys would short the whole matrix at rest, and latching keys reintroduce the
  0 Ω multi-close trap that no firmware check can catch.
- 6× resistors: 5.6 k, 11 k, 16 k, 1.1 k, 2.7 k, 3.9 k (1 % recommended)
- 1× 39 kΩ (Rload), 1× 10 nF (Csense)
- Row 1 / Col 1 are 0 Ω jumpers

For the bench rig, substitute the keypad module with **one jumper wire** (or 2×
1P4T rotary switches, commons tied), plus a 10-bit AVR board.

For a home-built keypad (pin-compatible with the commercial module): perfboard,
16× SPST-NO momentary 6 mm tactile switches, 1× 8-pin 0.1" male header, bare tinned bus wire (rows)
and insulated hookup wire (columns).

## Notes

- **1 % resistors** recommended; the tightest 14-count gaps leave little room for
  5 % drift stacking.
- **Ratiometric:** use VCC as the ADC reference (`REFS` = VCC), not the internal
  1.1 V bandgap — the whole scheme depends on VCC and ADC-ref scaling together.
- **Optional self-calibration:** on boot, capture each key's actual ADC once and
  store midpoints in EEPROM to absorb resistor tolerance — turns 5 % parts into
  effectively 1 % accuracy.
- Reproduce / re-tune: `python3 tools/ladder_optimizer.py` (fixed seed → stable
  output).
