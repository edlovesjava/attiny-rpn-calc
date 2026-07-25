# smartiny-key

16-key keypad → I²C events. **Addr `0x20` · `WHO_AM_I` `0x01` · ATtiny85 slave**

Decodes all 16 keys on **one ADC pin** using a base-4 resistor ladder, handles
debounce and sticky/latching modifier keys, and serves key events from a FIFO so
the host can poll at its own pace.

## The three layers

The module deliberately separates into stages that can be built, swapped and
tested independently:

| Layer | What it is | Interface out |
|---|---|---|
| **1. Key matrix** | 16 momentary switches in a passive 4×4 grid. No electronics. | 8-pin header: `R1–R4`, `C1–C4` |
| **2. Resistor ladder** | 6 resistors + `Rload` + `Csense`, encoding row/col as one voltage | single `SENSE` line |
| **3. Controller** | ATtiny85: ADC decode, debounce, modifiers, event FIFO, I²C slave | Qwiic (+ shared `INT`) |

Layer 1 matches the **commercial 4×4 module pinout**, so a home-built perfboard
keypad and an off-the-shelf one are drop-in swaps. Because the matrix is passive,
the *same* keypad can be driven by the 1-ADC ladder (ATtiny85), a classic 8-GPIO
scan (tinyAVR), or the 2-ADC variant — it stays a useful fixture whichever way
the platform goes.

## Status

- ✅ Ladder values locked — 14-count min ADC gap, 12-count PCINT wake margin
- ✅ Bench sketch + validation rig documented
- ⬜ Perfboard matrix build
- ⬜ Bench measurements vs predicted table
- ⬜ `core/` (decode, debounce, FIFO, modifiers) + host tests
- ⬜ I²C slave firmware, PCB

## Contents

```
docs/ladder.md              locked resistor values, decode table, C thresholds,
                            BOM, bench rigs, multi-press behaviour
tools/ladder_optimizer.py   the E24 search that produced those values
tools/ladder_test/          bench sketch: decodes 16 keys, reports ADC margin
```

## Key firmware rules

- **`keycode = 4·row + col`** (0–15); ADC codes *descend* as keycode ascends.
- **Ratiometric** — use VCC as the ADC reference, never the 1.1 V bandgap.
- **Emit on first settle, then require a return to idle** before the next key.
  This is what makes simultaneous presses benign (see `docs/ladder.md`
  § Multi-press behaviour).
- **Keep key 0 harmless.** `Rr0 = Rc0 = 0 Ω` makes keycode 0 an absorbing
  element, so spurious key-0 events are the likeliest collision artifact — never
  put `CLEAR`/`OFF` there.
- **Wake and decode share one wire**: idle `SENSE` = 0, any press crosses VIH and
  raises PCINT3, which wakes the MCU to run the ADC decode.

Registers: see `SMARTINY_KEY_REG_*` in
[`lib/smartiny-common/smartiny_regs.h`](../../lib/smartiny-common/smartiny_regs.h).
