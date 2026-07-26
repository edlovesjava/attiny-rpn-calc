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
each packing `[mode:4][keycode:4]`, stored in EEPROM — so which key
is SHIFT is configuration, not firmware. A calculator wanting HP-style `f`/`g`
just fills two slots.

| Mode | Behaviour |
|---|---|
| `OFF` | slot unused |
| `MOMENTARY` | active only while physically held |
| `STICKY` | **one-shot** — latches on press, applies to the *next* key, then auto-clears |
| `LOCK` | toggles on each press until pressed again (caps-lock) |

### Trigger and latch are independent

Modifier mode says **what the latch does**. The `TAPHOLD` mask (`0x1D`/`0x1E`,
one bit per keycode) says **when it triggers**. Keeping them separate is what
lets one mechanism serve very different keypads:

| | bit **clear** — triggers on `PRESS` | bit **set** — triggers on `LONG` |
|---|---|---|
| **modifier slot** | dedicated FUNC key: short press latches, no holding | short press is the key's normal function, hold latches |
| **no modifier slot** | ordinary key | dual-function key — host decides what `LONG` means |

A key with its tap-hold bit set **defers its normal function to release**, because
at press time the module cannot yet know whether a tap or a hold is coming. So:

| Gesture on a masked key | Events |
|---|---|
| tap (release before `HOLD_MS`) | `PRESS` + `RELEASE` — acted as a normal key |
| hold past `HOLD_MS` | `LONG` + `RELEASE`, **no `PRESS`** — consumed as a mode change |
| any press **while its modifier is engaged** | `LONG` + `RELEASE` — modifier disengages |

**The first event type tells the host how the press was interpreted:** `PRESS`
means "acted as a normal key", `LONG` means "consumed as a mode change". That
also gives an easy escape — once a modifier is on, *any* press of its key clears
it, short or long, so you never have to hold to cancel.

Two costs, so only mask keys that need it:

- **The tap fires on release, not press.** For a quick tap that is just your own
  press duration (~60–100 ms), not `HOLD_MS` — but it is a different feel.
- **A masked key cannot auto-repeat.** Holding is already spoken for, so never
  mask a key you want to hold down (backspace, cursor).

Worked examples of both arrangements: [`layouts.md`](layouts.md).

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

One LED, in strict priority order (highest first):

| Priority | Condition | Pattern | Reads as |
|---|---|---|---|
| 1 | **any key held** | solid on | talkback: "that decoded" |
| 2 | modifier **latched** (sticky one-shot) | fast blink ~8 Hz | "armed — next key consumes it" |
| 3 | modifier **locked / engaged** | brief flash every ~1.5 s | "armed and settled" |
| 4 | idle | off | |

**Talkback outranks modifier indication**, which matters: every keypress is
acknowledged, and the modifier pattern reappears the moment nothing is held. The
alternative — modifier always winning — would suppress talkback exactly when a
modifier is active, which is when you most want confirmation.

**Threshold pulse.** When `HOLD_MS` is reached with a key still down, the LED
pulses out of solid and back:

- ordinary key → **one blip** (~40 ms off, then solid) — the `LONG` event fired
- `TAPHOLD` key → **double blip** — a mode change is about to happen, so it gets a
  louder signal

You feel the threshold register without looking at the screen — a software
detent, and it tells you that you may now let go.

### The `TAPHOLD` shift sequence

| Moment | LED |
|---|---|
| press down | **solid on** — talkback, same as any key |
| still held at `HOLD_MS` | **double blip** — "shift will engage; release now" |
| release | **fast blink** — shift mode is ON, and stays blinking |
| press a normal key | solid while held, then back to fast blink |
| tap shift again | **off** — mode cleared |

A short tap never reaches the blip: solid on press, off on release, exactly like
any other key. The whole state of the feature is legible from one LED.

### Intensity is a second channel

PB1 carries hardware PWM (`OC1A`/`OC0B`), so the LED can be dimmed — and that is
worth more than just "less bright". Use **intensity to separate transient events
from persistent state**, orthogonally to blink pattern:

| State | Pattern | Intensity |
|---|---|---|
| key held — talkback | solid | **bright** |
| threshold pulse | blip | **bright** |
| modifier latched | fast blink | **dim** |
| modifier locked / engaged | brief periodic flash | **dim** |
| idle | off | — |

Now talkback and modifier state stay distinguishable even when both are lit, and
a glance tells you whether the LED is *reacting* or *remembering*. `LED_LEVELS`
(`0x20`) packs `[bright:4][dim:4]`, so a host can also just turn the whole thing
down at night.

Levels run through a **gamma 2.2 table**, because perceived brightness is roughly
`duty^(1/2.2)` — a linear duty ramp bunches badly at the top. Practical values are
**dim 6–8, bright 15**; level 4 and below is barely visible.

```
level  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
duty   0   1   3   7  14  23  34  48  64  83 105 129 156 186 219 255
```

### What the intensity actually costs — and where the real cost is

At 1 kΩ series on 3.3 V, full brightness is ~1.5 mA and dim (level 6) is ~0.2 mA.
Combined with a 3 % duty flash, a locked modifier's LED averages about **6 µA**.

But the LED is not the expensive part. **Keeping the MCU awake to animate it is.**
An ATtiny85 running at 8 MHz draws ~3–5 mA — twenty times the LED at full
brightness. So an indicator pattern is really a *wake-duty* decision:

- **Transient patterns are free.** The chip is already awake because you are
  pressing keys.
- **A persistent pattern must not hold the chip awake.** That is the actual reason
  `LOCK` uses a brief periodic flash rather than a 50 % blink: WDT-wake every
  ~1.5 s, flash ~40 ms, sleep again → ~3 % awake duty, ~0.1 mA average. Holding
  the chip awake to blink at 8 Hz instead would cost ~3 mA — thirty times more,
  and none of it in the LED.

Restating the earlier §9.5 point more precisely: it is not LED current that
matters at this scale, it is **how long the pattern forces the processor to stay
awake**.

### Implementation: soft PWM, not hardware

Use the same main-loop soft PWM as `smartiny-led` rather than Timer1 hardware
PWM. One LED makes the cost trivial, the pattern is already written and tested,
and it avoids a real hazard:

> ⚠️ **ATtiny85 Timer1 has complementary outputs, and `OC1A'` sits on PB0 — which
> is SDA.** A `COM1A` setting that enables the complement would drive the I²C data
> line and destroy the bus. If you ever do move to hardware PWM, verify against
> the datasheet that the complementary output stays disconnected. Timer0 is also
> unavailable, since `millis()` is what times debounce and `HOLD_MS`.

Note that soft PWM stops when the MCU sleeps — which is fine, because every
pattern that needs animating already implies being awake.

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
| `PRESS` key 15, **mods 1** | `0x1F` | solid on — talkback |
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

**G — `TAPHOLD` shift on key 15** (`MOD0_CFG` = `0x4F`)

*Tap — acts as a normal key. Nothing is emitted on press, because the module
cannot yet know whether this is a tap or a hold.*

| Moment | Event | Byte | LED |
|---|---|---|---|
| press | *(deferred)* | — | solid on |
| release (before threshold) | `PRESS` key 15, mods 0 | `0x0F` | off |
| | `RELEASE` key 15, mods 0 | `0x4F` | |

*Hold — engages shift. No `PRESS`, which is how the host knows the normal
function was suppressed.*

| Moment | Event | Byte | LED |
|---|---|---|---|
| press | *(deferred)* | — | solid on |
| at `HOLD_MS`, still held | `LONG` key 15, **mods 1** | `0x9F` | **double blip** |
| release | `RELEASE` key 15, mods 1 | `0x5F` | fast blink — shift ON |
| press key 5 | `PRESS` key 5, mods 1 | `0x15` | solid on |
| release key 5 | `RELEASE` key 5, **mods 1** | `0x55` | fast blink — not consumed |

*Cancel — any press clears it, and this one fires immediately on press, since a
tap and a hold would do the same thing.*

| Moment | Event | Byte | LED |
|---|---|---|---|
| press | `LONG` key 15, **mods 0** | `0x8F` | solid on |
| release | `RELEASE` key 15, mods 0 | `0x4F` | off |

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

Defaults on a blank EEPROM: SHIFT = keycode 15 in `STICKY` mode (switch to
`TAPHOLD` with `--set-mod 0 taphold 15` to make it dual-purpose), `LED_MODE` =
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
