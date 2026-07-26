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

## Example event sequences

Setup below: SHIFT = keycode 15 in `STICKY` mode, `HOLD_MS` = 500 ms.

**A — plain tap of key 5**

| Event | Byte | LED |
|---|---|---|
| `PRESS` key 5, mods 0 | `0x05` | solid on |
| `RELEASE` key 5, mods 0 | `0x45` | off |

**B — sticky SHIFT, then key 5** (the flow that matters)

| Event | Byte | LED |
|---|---|---|
| `PRESS` key 15, **mods 1** | `0x1F` | fast blink |
| `RELEASE` key 15, **mods 1** | `0x5F` | fast blink — *latch survives release* |
| `PRESS` key 5, **mods 1** | `0x15` | solid on — key 5 carries the modifier |
| `RELEASE` key 5, **mods 0** | `0x45` | off — one-shot consumed |

Two conventions to note. The modifier snapshot is the state **after** the event
is processed, so SHIFT's own `PRESS` already reports `mods=1` — the host learns
about the latch from the event stream and never has to race a read of
`MODIFIERS`. And the one-shot is consumed at the **`PRESS`** of the next key, so
that key's `RELEASE` reporting `mods=0` is the observable marker that the latch
was spent. Hosts should act on `PRESS`.

**C — long press of key 7**

| Event | Byte | LED |
|---|---|---|
| `PRESS` key 7 | `0x07` | solid on |
| *(500 ms later, still held)* `LONG` key 7 | `0x87` | blip off 40 ms → solid |
| `RELEASE` key 7 | `0x47` | off |

**D — long press with auto-repeat** (key 3 as a backspace, say)

`PRESS 0x03` → `LONG 0x83` → `REPEAT 0xC3` → `REPEAT 0xC3` → … → `RELEASE 0x43`

**E — `LOCK` mode SHIFT** (`MOD0_CFG` = lock)

| Event | Byte | Note |
|---|---|---|
| `PRESS` key 15, mods 1 | `0x1F` | lock engaged |
| `RELEASE` key 15, mods 1 | `0x5F` | |
| `PRESS` key 5, mods 1 | `0x15` | shifted |
| `RELEASE` key 5, **mods 1** | `0x55` | still locked — lock is *not* consumed |
| `PRESS` key 15, **mods 0** | `0x0F` | pressing SHIFT again clears the lock |
| `RELEASE` key 15, mods 0 | `0x4F` | |

**F — two keys pressed at once** (5 first, then 6 while still held)

| Event | Byte | Note |
|---|---|---|
| `PRESS` key 5 | `0x05` | key 5 settles |
| *(key 6 also pressed)* | — | **no event** — still *held*, changes ignored |
| *(both released)* `RELEASE` key 5 | `0x45` | back to idle |

Only key 5 is ever reported. This is the release-to-idle policy from
[`ladder.md`](ladder.md) doing its job: the collision voltage never becomes an
event.

## Testing

**The ATtiny85 has no spare pin for a debug UART** — PB0/PB2 are the bus, PB3 is
`SENSE`, PB1 is the LED, PB4 is `INT`. That constraint is exactly why the
architecture is shaped the way it is, and it gives three test layers instead:

1. **Host unit tests — where the real testing happens.** `key_core` is pure C, so
   feed it synthetic ADC values and a fake clock and assert the emitted event
   stream. **The sequences above are the test vectors**: scenario B pins down
   sticky consumption, C pins down hold timing, F pins down collision handling.
   No hardware, no bus, runs in milliseconds.
2. **The bus, via `tools/key_monitor.py`.** Drains `EVENT_FIFO` over I²C and
   pretty-prints the decoded stream — this *is* the debug console.

   ```console
   $ ./key_monitor.py --bus 3
   smartiny-key at 0x20, firmware v0.1
   [   1.204] 0x1F  PRESS   key=15 (r3,c3) mods=01
   [   1.336] 0x5F  RELEASE key=15 (r3,c3) mods=01
   [   2.011] 0x15  PRESS   key=5  (r1,c1) mods=01
   [   2.140] 0x45  RELEASE key=5  (r1,c1)
   ```

   It also warns on `FIFO OVERFLOW`, which means the host is polling too slowly.
   `--selftest` checks the decoder with no hardware attached.
3. **The LED, before any bus exists.** Talkback works with nothing but power, so
   during bring-up it confirms the ADC decode and debounce are alive before I²C
   is in the picture. Add the logic analyzer on SDA/SCL when the question becomes
   "why did the master not see that".

If you truly need a printf during bring-up, temporarily bit-bang serial on
**PB4** — `INT` is the last pin to become load-bearing.

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

### How the EEPROM actually gets written

**Over I²C, at runtime — no programmer involved.** That is the whole point of
putting the config in registers: write `MOD0_CFG`, then write `0x5A` to `SAVE`.

```console
$ ./key_monitor.py --bus 3 --set-mod 0 sticky 15 --hold-ms 500 --save
  MOD0_CFG <- sticky on key 15
  config committed to EEPROM
```

Configuration is **bus data, not firmware** — so a soldered-down '85 stays fully
configurable, and the host itself can reconfigure the keypad at boot.

Two other paths, for completeness:

- **Bulk provisioning at flash time.** `avrdude -U eeprom:w:defaults.hex:i`
  writes a whole EEPROM image alongside the firmware — useful for setting up a
  batch of identical modules.
- **In-circuit ISP after soldering.** Put a 6-pin ISP header on the module and
  you can reflash *firmware* without desoldering anything.

> **This is the payoff of never repurposing PB5.** RESET is intact, so ordinary
> low-voltage ISP keeps working on a finished board forever. Had we taken PB5 as
> a GPIO (the tempting way to get a 4th LED early on), reprogramming would need a
> high-voltage programmer. One socket-vs-soldered caveat: ISP uses PB0/PB1/PB2,
> which are SDA/LED/SCL — so **unplug the Qwiic cable before programming**, or
> the rest of the bus fights the programmer.

**Socket or solder?** Keep a DIP-8 socket on perfboard prototypes — chips get
swapped constantly while the firmware is in flux, and a socket costs pennies. On
a real PCB, solder the part and fit the ISP header instead: with RESET preserved,
in-circuit reflashing makes the socket unnecessary.

> Keycode 15 is deliberate: it is the *farthest* code from the keycode-0
> absorbing element, so a modifier is the key least likely to be produced
> spuriously by a two-key collision. And per `ladder.md`, key 0 must stay
> harmless — never put SHIFT or `CLEAR` there.
