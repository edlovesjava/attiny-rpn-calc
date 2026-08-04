# Keypad Resistor-Ladder v2 — Build Sheet

Updated resistor set for decoding the passive 4×4 matrix on **one ADC pin** (PB3).
This v2 keeps the same architecture as `ladder.md`, with recalculated values.

## v2 resistor set

| Resistor | Value |
|---|---|
| Rr0 (row 0) | 0 Ω (wire) |
| Rr1 (row 1) | 5.6 kΩ |
| Rr2 (row 2) | 10 kΩ |
| Rr3 (row 3) | 22 kΩ |
| Rc0 (col 0) | 0 Ω (wire) |
| Rc1 (col 1) | 1.0 kΩ |
| Rc2 (col 2) | 2.0 kΩ |
| Rc3 (col 3) | 3.3 kΩ |
| Rload | 47 kΩ |
| Csense | 10 nF |

`keycode = 4·row + col` (0–15), and for a pressed key:

- `Rtot = Rr_i + Rc_j`
- `V_sense/VCC = Rload / (Rtot + Rload)`
- `ADC = round(1023 * V_sense/VCC)`

## Full decode table (10-bit ADC)

| key | row | col | Rtot (Ω) | frac | ADC | gap to next |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 0 | 0 | 1.000000 | 1023 | 21 |
| 1 | 0 | 1 | 1000 | 0.979167 | 1002 | 21 |
| 2 | 0 | 2 | 2000 | 0.959184 | 981 | 25 |
| 3 | 0 | 3 | 3300 | 0.934394 | 956 | 42 |
| 4 | 1 | 0 | 5600 | 0.893536 | 914 | 17 |
| 5 | 1 | 1 | 6600 | 0.876866 | 897 | 16 |
| 6 | 1 | 2 | 7600 | 0.860806 | 881 | 21 |
| 7 | 1 | 3 | 8900 | 0.840787 | 860 | 17 |
| 8 | 2 | 0 | 10000 | 0.824561 | 843 | 14 |
| 9 | 2 | 1 | 11000 | 0.810345 | 829 | 14 |
| 10 | 2 | 2 | 12000 | 0.796610 | 815 | 18 |
| 11 | 2 | 3 | 13300 | 0.779436 | 797 | 100 |
| 12 | 3 | 0 | 22000 | 0.681159 | 697 | 10 |
| 13 | 3 | 1 | 23000 | 0.671429 | 687 | 10 |
| 14 | 3 | 2 | 24000 | 0.661972 | 677 | 12 |
| 15 | 3 | 3 | 25300 | 0.650519 | 665 | — |

Minimum adjacent gap: **10 counts** (between keys 12–13 and 13–14).

## 5 V expected voltages

| key | Rtot (Ω) | frac | Vsense @ 5.0V |
|---:|---:|---:|---:|
| 0 | 0 | 1.000000 | 5.000 V |
| 1 | 1000 | 0.979167 | 4.896 V |
| 2 | 2000 | 0.959184 | 4.796 V |
| 3 | 3300 | 0.934394 | 4.672 V |
| 4 | 5600 | 0.893536 | 4.468 V |
| 5 | 6600 | 0.876866 | 4.384 V |
| 6 | 7600 | 0.860806 | 4.304 V |
| 7 | 8900 | 0.840787 | 4.204 V |
| 8 | 10000 | 0.824561 | 4.123 V |
| 9 | 11000 | 0.810345 | 4.052 V |
| 10 | 12000 | 0.796610 | 3.983 V |
| 11 | 13300 | 0.779436 | 3.897 V |
| 12 | 22000 | 0.681159 | 3.406 V |
| 13 | 23000 | 0.671429 | 3.357 V |
| 14 | 24000 | 0.661972 | 3.310 V |
| 15 | 25300 | 0.650519 | 3.253 V |

## Firmware decode thresholds (midpoints)

```c
// ADC >= KEY_THRESH[k]  =>  at least keycode k (codes descend with keycode)
static const uint16_t KEY_THRESH[16] = {
  1013, 992, 969, 935, 906, 889, 871, 852,
   836, 822, 806, 747, 692, 682, 671, 332
};
#define KEY_NONE 0xFF

uint8_t decode_key(uint16_t adc) {
    if (adc < KEY_THRESH[15]) return KEY_NONE;   // idle / no press
    for (uint8_t k = 0; k < 16; k++)             // first (smallest) k that fits
        if (adc >= KEY_THRESH[k]) return k;
    return KEY_NONE;
}
```

## Power-off DMM check (quick)

With power **OFF**, meter in Ω mode:

- one probe on **+V rail** (row resistor feed)
- one probe on **SENSE**
- close exactly one row + one column (one simulated keypress)

Expected resistance is exactly `Rrow + Rcol`:

`0, 1k, 2k, 3.3k, 5.6k, 6.6k, 7.6k, 8.9k, 10k, 11k, 12k, 13.3k, 22k, 23k, 24k, 25.3k`.
