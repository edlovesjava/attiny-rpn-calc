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
the *same* keypad can be driven by the 1-ADC ladder (ATtiny85) or a classic
8-GPIO scan (tinyAVR) — it stays a useful fixture whichever way the platform goes.

## Pin budget

| Pin | Use |
|---|---|
| PB0 / PB2 | SDA / SCL |
| PB3 | `SENSE` — ladder decode **and** PCINT wake |
| PB1 | status LED (talkback + modifier state) |
| PB4 | shared `INT` line (open-drain to master) |
| PB5 | RESET — kept |

Two free pins, two jobs: **one** LED, because `INT` is what lets the master
deep-sleep and wake on a keypress. Drop `INT` and you could have two LEDs and
poll-only — architecture §11 decision 3.

## Two keypads = 32 keys

Yes — this is what the EEPROM `I2C_ADDR` register was for. Keypads occupy
**`0x20`–`0x23`**, so up to four boards coexist; two gives 32 keys. Nothing in the
module changes.

Three things the host must handle:

**1. Both ship at `0x20`.** Two identical modules answer the same address and you
**cannot** re-address one of a colliding pair — the write reaches both. So set the
second board's address **while it is alone**, on the dock or off the bus, *before*
joining them. This is the sharpest practical gotcha in the whole scheme.

```console
$ ./key_monitor.py --bus 3 --addr 0x20   # second board, ALONE
  ... write I2C_ADDR = 0x21, SAVE
$ # now both can share the bus
```

**2. `INT` needs no changes at all.** It is open-drain wired-OR (§9.6), so any
number of keypads share the one wire; the master wakes and reads `KEY_STATUS` to
find who has events. **This is the payoff of choosing a shared attention line over
a per-slot interrupt bus** — scaling to N keypads costs zero extra wires.

**3. ⚠️ Modifiers do not span modules.** Each keypad tracks its own latches, so a
`STICKY` shift on board A reports `mods=0` on board B's events. The `mod_snapshot`
in an event is only *that module's* view.

> The host must merge: read `MODIFIERS` from every keypad, OR them together, and
> apply its own union rather than trusting a per-event snapshot across boards. The
> alternative — a duplicate shift key on each board — wastes a key and is worse.

Keycodes stay 0–15 per module, so the host's logical key is
`(module_index, keycode)`. That is a keymap concern, not a protocol one.

## Modifiers, hold and feedback

Any of the 16 keys can be a modifier (4 slots, EEPROM-stored), in `MOMENTARY`,
`STICKY` (one-shot) or `LOCK` mode. A separate tap-hold mask decides *when* a key
triggers — on press, or only on a long hold (so the short press stays the key's
normal function). Long-press is reported as a distinct `LONG`
event that fires *while the key is still held*. The single LED does talkback
(solid while held), latched-modifier (fast blink) and lock (slow flash).

Full design: [`docs/keys-and-feedback.md`](docs/keys-and-feedback.md).

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
docs/layouts.md             worked keymaps: decimal RPN and hex entry
tools/key_monitor.py        I2C event monitor + EEPROM config tool
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
