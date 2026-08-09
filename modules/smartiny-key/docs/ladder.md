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

## Why not two ADCs? (closed)

A recurring instinct is to spend a second ADC pin — one for rows, one for columns
— to get bigger margins. **It cannot work on a passive matrix.**

A keypress is a *single two-terminal short*, so however you wire the network, the
press contributes exactly one resistance, `Rr_i + Rc_j`. Solving the two-node case
(rows reaching ADC1, columns reaching ADC2, both nodes independently biased) gives
`V_A` and `V_B` as functions of **that sum alone** — `V_B` merely mirrors `V_A`.
Two ADCs measure the same scalar twice: redundancy, not independence, and
redundancy is cheaper as extra samples on one pin.

Genuine two-axis information requires *driving* one axis, i.e. GPIO scanning —
which is exactly the thing the '85 has no pins for, and the reason the base-4
scheme exists.

Two further points settle it:

- **There is no free ADC pin.** PB3 is `SENSE`; the remaining ADC-capable pins are
  PB2 (SCL), PB5 (RESET) and PB4 — which is the shared `INT` line, and with it
  system deep-sleep wake.
- **What would help needs different hardware.** Two independent *8-key* ladders
  would roughly triple the spacing (mean gap 49 vs 23 counts), but 8 independent
  switches per ADC means 16 independent switches — not a matrix, so a custom board
  that also forfeits commercial-keypad compatibility, and still costs `INT`.

**If margins measure tight, use EEPROM self-calibration instead.** Tolerance is
what eats margin (1 % ≈ ±5 counts against a 6-count worst case); capturing each
key's real ADC value once at setup removes that error entirely, at the cost of
firmware rather than a pin.

## Bench validation

Before committing the values to a PCB, prove them on a breadboard with any
10-bit AVR (Uno / Nano / Pro Mini) reading `SENSE`. Sketch:
[`tools/ladder_test/ladder_test.ino`](../tools/ladder_test/ladder_test.ino).

### DMM bench — no MCU required

The quickest first pass: build the ladder, sweep it with two jumpers, read it with
a multimeter. This validates the *resistors* before any firmware is involved, and
it gives **two independent checks** — one of which needs no power supply at all.

#### Schematic

```
      +V ──┬──[ wire ]── ROW0 ──┐
           ├──[ 5.6k ]── ROW1 ──┤
           ├──[ 11k  ]── ROW2 ──┤   ← JUMPER A picks one row
           └──[ 16k  ]── ROW3 ──┘
                                │
                              PRESS        ← the two jumpers meet here
                                │
           ┌──[ wire ]── COL0 ──┤
           ├──[ 1.1k ]── COL1 ──┤   ← JUMPER B picks one column
           ├──[ 2.7k ]── COL2 ──┤
           └──[ 3.9k ]── COL3 ──┘
           │
        SENSE ──┬──[ 39k ]── GND        (Rload)
                └──● DMM red probe       (DMM black → GND)

      keycode = 4 × rowA + colB
```

`PRESS` is the floating rail from § Emulating the matrix — it must **not** go to
ground. Closing one row and one column onto it reproduces exactly the short a real
key makes.

#### Breadboard placement

Nine tie-point strips. Column numbers are arbitrary — just keep them apart:

| Strip | Node | Component to fit |
|---|---|---|
| 1 | `ROW0` | plain wire from **+ rail** |
| 3 | `ROW1` | **5.6 kΩ** from + rail |
| 5 | `ROW2` | **11 kΩ** from + rail |
| 7 | `ROW3` | **16 kΩ** from + rail |
| 12 | `PRESS` | *(nothing — the two jumpers meet here)* |
| 17 | `COL0` | plain wire to **SENSE** |
| 19 | `COL1` | **1.1 kΩ** to SENSE |
| 21 | `COL2` | **2.7 kΩ** to SENSE |
| 23 | `COL3` | **3.9 kΩ** to SENSE |
| 27 | `SENSE` | **39 kΩ** down to **− rail**; DMM red probe here |

- **Jumper A**: strip 12 → one of {1, 3, 5, 7}
- **Jumper B**: strip 12 → one of {17, 19, 21, 23}

Leave `Csense` out for DMM work — a 10 nF cap does nothing for a DC measurement.
It is only there to filter for the ADC.

#### Check 1 — resistance, with the power OFF

The cleanest check, and it needs no supply. DMM in **Ω**, probes on the **+ rail**
and **SENSE**:

> reading = `Rr_i + Rc_j` — the `Rtot` column below, nothing else.

The only path between those two points runs through the two jumpers; every other
row and column dead-ends. So this isolates the ladder from `Rload` entirely and
proves you grabbed the right parts, with no dependence on supply accuracy.

#### Check 2 — voltage, powered

DMM in **DC volts**, black on **− rail**, red on **SENSE**. Then:

> **Measure the supply too, and compare the *ratio*, not the volts.**
> `V_sense ÷ V_supply` should equal the `frac` column.

The divider is ratiometric, so the fraction is the real specification — comparing
raw volts just imports your supply's error into the result.

| key | r,c | Rtot (Ω) | frac | V @3.3 V | V @5.0 V |
|---|---|---|---|---|---|
| 0 | 0,0 | 0 | 1.0000 | 3.300 | 5.000 |
| 1 | 0,1 | 1100 | 0.9726 | 3.209 | 4.863 |
| 2 | 0,2 | 2700 | 0.9353 | 3.086 | 4.676 |
| 3 | 0,3 | 3900 | 0.9091 | 3.000 | 4.545 |
| 4 | 1,0 | 5600 | 0.8744 | 2.886 | 4.372 |
| 5 | 1,1 | 6700 | 0.8534 | 2.816 | 4.267 |
| 6 | 1,2 | 8300 | 0.8245 | 2.721 | 4.123 |
| 7 | 1,3 | 9500 | 0.8041 | 2.654 | 4.021 |
| 8 | 2,0 | 11000 | 0.7800 | 2.574 | 3.900 |
| 9 | 2,1 | 12100 | 0.7632 | 2.519 | 3.816 |
| 10 | 2,2 | 13700 | 0.7400 | 2.442 | 3.700 |
| 11 | 2,3 | 14900 | 0.7236 | 2.388 | 3.618 |
| 12 | 3,0 | 16000 | 0.7091 | 2.340 | 3.545 |
| 13 | 3,1 | 17100 | 0.6952 | 2.294 | 3.476 |
| 14 | 3,2 | 18700 | 0.6759 | 2.231 | 3.380 |
| 15 | 3,3 | 19900 | 0.6621 | 2.185 | 3.311 |

**Sanity check first:** keycode 0 (both jumpers on the 0 Ω legs) must read
*exactly* the supply voltage. Anything less means a jumper is not seated.

#### DMM caveats

- **Check your meter's input impedance.** At 10 MΩ it loads `Rload` to 38.85 kΩ —
  under 1 ADC-count equivalent, ignorable. At **1 MΩ** it loads to 37.5 kΩ, which
  shifts the high keycodes by up to **~9 counts** and will make good resistors look
  bad. Bench meters are usually 10 MΩ; cheap pocket ones are not always.
- Nothing here is time-critical, so take your time — but do let the reading settle
  before trusting the last digit.
- A wrong reading on **four keys sharing a row or column** localises the faulty
  resistor exactly as in § Diagnosing a bad reading below.

### Run sheet (with an Arduino)

Everything you need for one bench session, in order.

**1. Parts** — 6 resistors, `Rload`, `Csense`, **one jumper wire**, one Arduino.

| Node | Connect | Value |
|---|---|---|
| Row0 | → VCC | wire (0 Ω) |
| Row1 | → VCC | 5.6 kΩ |
| Row2 | → VCC | 11 kΩ |
| Row3 | → VCC | 16 kΩ |
| Col0 | → `SENSE` | wire (0 Ω) |
| Col1 | → `SENSE` | 1.1 kΩ |
| Col2 | → `SENSE` | 2.7 kΩ |
| Col3 | → `SENSE` | 3.9 kΩ |
| `SENSE` | → GND | 39 kΩ (`Rload`) |
| `SENSE` | → GND | 10 nF (`Csense`, keep near the pin) |
| `SENSE` | → **A0** | — |

Eight breadboard strips (4 row nodes, 4 col nodes) plus a `SENSE` strip. Power the
ladder from **the Arduino's own VCC** — the decode is ratiometric, so the rail and
the ADC reference must be the same supply.

> If your Nano is wired as the ISP programmer, **pull the 10 µF cap off its
> RESET** before uploading this sketch — that cap exists to *stop* auto-reset, so
> it also blocks a normal upload.

**2. Flash** `ladder_test.ino`, open the serial monitor at **115200**.

**3. Sweep.** Move the single jumper across all 16 row×col combinations. One wire
can only ever connect one row to one column, so an invalid reading is impossible
by construction.

**4. Read the output.**

```
adc     key     r,c     design  err     margin
1023    0       0,0     1023    0       14
873     5       1,1     873     0       10
725     12      3,0     725     0       6
677     15      3,3     677     0       6
```

| Column | Means |
|---|---|
| `key` | decoded keycode — must match the jumper position |
| `err` | measured − predicted; **the diagnostic column** |
| `margin` | counts to the nearest decision boundary; **the pass column** |

### Pass / fail

Predicted margins are **6–14 counts** (worst at keys 12, 13, 15), so:

| `margin` | Verdict |
|---|---|
| ≥ 4 | good |
| 1–3 | decodes, but marginal — expect occasional flicker; consider EEPROM self-calibration |
| ≤ 0 | misreads — do not proceed to a PCB |

All 16 keys must decode correctly, and `err` should sit within roughly ±5.

### Diagnosing a bad reading

`err` localises a wrong resistor, because each one affects exactly four keys:

| Keys with consistent `err` | Suspect |
|---|---|
| 0–3 | Row0 wire (should be 0 Ω) |
| 4–7 | `Rr1` 5.6 kΩ |
| 8–11 | `Rr2` 11 kΩ |
| 12–15 | `Rr3` 16 kΩ |
| 1, 5, 9, 13 | `Rc1` 1.1 kΩ |
| 2, 6, 10, 14 | `Rc2` 2.7 kΩ |
| 3, 7, 11, 15 | `Rc3` 3.9 kΩ |
| **all 16, same direction** | `Rload` 39 kΩ — or the ADC reference isn't VCC |

An `err` pattern spanning one row *and* one column means you swapped two values.
`err` drifting the same way across every key is the one case that wants the
optimizer re-run rather than a parts swap.

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

### Extra checks

Pass/fail thresholds and per-resistor diagnosis are in the
[run sheet](#pass--fail) above. Two additional checks:

| Check | Expect |
|---|---|
| Monotonicity | key *n* never reads as *n*±1 |
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
- **One keypad, two decode strategies.** The same passive board can be driven by
  the 1-ADC ladder ('85) or a classic 8-GPIO scan (tinyAVR) — so it stays a
  useful test fixture no matter which way the platform goes.

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

- **Switch contact current is ~56–128 µA** (`VCC / (R_total + Rload)`, so 85 µA at
  keycode 0 and 56 µA at keycode 15 on a 3.3 V rail). Any tactile switch rating —
  even a modest "12 V DC 50 mA" — has ~500× margin. This is low-level *dry-circuit*
  switching, but it is the same regime as every `INPUT_PULLUP` button read, and
  tactile domes wipe as they snap; combined with the ~50 Ω contact-resistance
  budget it is a non-issue. Gold-plated contacts are optional insurance.
- **1 % resistors** recommended; the tightest 14-count gaps leave little room for
  5 % drift stacking.
- **Ratiometric:** use VCC as the ADC reference (`REFS` = VCC), not the internal
  1.1 V bandgap — the whole scheme depends on VCC and ADC-ref scaling together.
- **Optional self-calibration:** on boot, capture each key's actual ADC once and
  store midpoints in EEPROM to absorb resistor tolerance — turns 5 % parts into
  effectively 1 % accuracy.
- Reproduce / re-tune: `python3 tools/ladder_optimizer.py` (fixed seed → stable
  output).
