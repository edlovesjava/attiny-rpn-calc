# smartiny-key — modifiers, hold, and local feedback

How the keypad turns 16 switches into a usable input device: sticky modifiers,
long-press, and a single status LED that has to earn its pin.

## Pin budget — why exactly one LED

| Pin | Use |
|---|---|
| PB0 | SDA |
| PB2 | SCL |
| PB3 | `SENSE` — ladder decode **and** PCINT wake |
| PB5 | RESET (kept, for easy reflashing) |
| **PB1** | **status LED** |
| **PB4** | **shared `INT` line** (open-drain to master) |

Two free pins, two jobs. The `INT` line is what lets the master deep-sleep and
wake instantly on a keypress (architecture §9.6) — give that up and the whole
~500 h power story goes with it. So the keypad gets **one** LED, and it has to
be worth it.

> Alternative if you drop `INT` and accept poll-only: two LEDs on PB1/PB4. That
> is architecture §11 decision 3.

**Why one LED is still worth a pin, when an OLED exists.** The two answer
different questions. The OLED shows *content* — the stack, the mode — and lives
where your eyes go to read a result. The LED shows *did that just register* and
*is shift armed*, which you need **while looking at the keypad**, instantly, and
before any I²C round trip. It is also the only feedback that works before the
bus is even brought up, which makes keypad bring-up far easier.

## Modifier model

Any of the 16 keys can be a modifier. There are 4 slots (`MOD0_CFG`…`MOD3_CFG`),
each packing `[mode:2][reserved:2][keycode:4]`, stored in EEPROM — so which key
is SHIFT is configuration, not firmware. A calculator wanting HP-style `f`/`g`
just fills two slots.

| Mode | Behaviour |
|---|---|
| `OFF` | slot unused |
| `MOMENTARY` | active only while physically held |
| `STICKY` | **one-shot** — latches on press, applies to the *next* key, then auto-clears |
| `LOCK` | toggles on each press until pressed again (caps-lock) |

**Sticky is the default and the important one.** It is what makes the whole
single-ADC design work: the user never has to physically hold two keys, so the
ladder never sees two closed switches. The feature that improves ergonomics is
the same feature that keeps the analog front-end unambiguous — see
[`ladder.md`](ladder.md) § Multi-press behaviour.

`MODIFIERS` (`0x11`) reports the live latch state, and every event carries a
2-bit snapshot of it, so the host always knows the context a key was pressed in
without racing to read a register.

## Press, hold, and release

`HOLD_MS` (`0x17`, units of 10 ms, default ~500 ms) sets the long-press
threshold. The module emits **facts, not interpretations**:

| Gesture | Events emitted |
|---|---|
| short tap | `PRESS` → `RELEASE` |
| long hold | `PRESS` → `LONG` (at threshold, while still held) → `RELEASE` |
| held with repeat enabled | `PRESS` → `LONG` → `REPEAT`… → `RELEASE` |

`LONG` fires **at the threshold while the key is still down**, not on release —
so the user gets feedback the moment it registers rather than after letting go.
The host decides what a long press *means*; the keypad never guesses. This is
the same division of labour as modifiers: the module reports which were latched,
the host decides what they do.

## LED behaviour

One LED, four distinguishable states, in priority order (highest first):

| State | Pattern | Reads as |
|---|---|---|
| Modifier **locked** | brief flash every ~1.5 s | "armed and settled" |
| Modifier **latched** (sticky, one-shot) | fast blink ~8 Hz, 50 % | "armed, urgent — next key consumes it" |
| Key **held** | solid on | talkback: "that decoded" |
| Idle | off | |

So pressing SHIFT latches it and the LED immediately goes to fast blink,
persisting *after release* — because the latch persists — until the next key
consumes it and the LED returns to off. Press any normal key and the LED is
solid only while held.

**Long-press confirmation:** when `LONG` fires, the LED blips **off for ~40 ms
then back to solid**. You feel the long-press register without looking at the
screen — the same idea as a mechanical detent.

Rates are chosen to be unmistakable and cheap: fast blink is transient so its
50 % duty costs nothing, while `LOCK` can persist indefinitely, hence a ~3 % duty
flash instead of a 50 % blink. See architecture §9.5 — average current is the
whole game.

`LED_MODE` (`0x1C`) enables talkback and modifier indication independently, or
hands the LED to the host entirely (`LED_MANUAL`, driven via `LED_LOCAL` `0x16`)
so a host can use it for its own signalling.

## Configuration and persistence

| Reg | Name | Meaning |
|---|---|---|
| `0x14` | `DEBOUNCE_MS` | debounce window |
| `0x15` | `REPEAT_CFG` | auto-repeat enable / rate |
| `0x17` | `HOLD_MS` | long-press threshold, ×10 ms |
| `0x18`–`0x1B` | `MOD0..3_CFG` | `[mode:2][rsvd:2][keycode:4]` |
| `0x1C` | `LED_MODE` | talkback / modifier / manual |
| `0x1F` | `SAVE` | write `0x5A` to commit the above to EEPROM |

Config changes take effect **immediately** but only persist on an explicit
`SAVE`. That keeps a host that rewrites `DEBOUNCE_MS` in a loop from burning
through EEPROM endurance, and lets you try settings freely knowing a power cycle
restores the saved set. (`I2C_ADDR` is the exception — it persists on write,
because you need a new address to survive the very next power-up.)

Defaults on a blank EEPROM: SHIFT = keycode 15 in `STICKY` mode, `LED_MODE` =
talkback + modifier, `HOLD_MS` = 500 ms.

> Keycode 15 is deliberate: it is the *farthest* code from the keycode-0
> absorbing element, so a modifier is the key least likely to be produced
> spuriously by a two-key collision. And per `ladder.md`, key 0 must stay
> harmless — never put SHIFT or `CLEAR` there.
