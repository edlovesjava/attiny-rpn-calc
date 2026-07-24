# Keypad Resistor-Ladder — Build Sheet

Concrete values for decoding the passive 4×4 matrix on **one ADC pin** (PB3),
produced by [`tools/ladder_optimizer.py`](../tools/ladder_optimizer.py). See
`architecture.md` §6.1 (decode) and §9.6 (wake) for the theory.

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

## BOM (per keypad)

- 1× passive 4×4 matrix keypad module (8 leads, no controller)
- 6× resistors: 5.6 k, 11 k, 16 k, 1.1 k, 2.7 k, 3.9 k (1 % recommended)
- 1× 39 kΩ (Rload), 1× 10 nF (Csense)
- Row 1 / Col 1 are 0 Ω jumpers

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
