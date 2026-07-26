# smartiny-led — hardware

ATtiny85 I²C slave driving direct-drive indicator LEDs. Deliberately the
simplest board in the family: it exists to get the *bus* plumbing right.

## Pin budget — why 3 LEDs, not 4

The ATtiny85 has 6 I/O pins. PB5 is RESET (repurposing it needs a high-voltage
programmer and costs easy reflashing), and USI I²C takes two more:

| Pin | Function | Notes |
|---|---|---|
| PB0 | **SDA** | USI, fixed |
| PB1 | **LED0** | also OC1A if hardware PWM is wanted later |
| PB2 | **SCL** | USI, fixed |
| PB3 | **LED1** | |
| PB4 | **LED2** | also OC1B |
| PB5 | RESET | leave as reset |

That leaves **3 free GPIO → 3 direct-drive LEDs** in v1.

**v1 populates 3 and stays dumb.** The point of Board 0 is the slave skeleton,
not the light show.

## Expansion path — more LEDs on the same 3 pins

| Approach | LEDs | Simultaneous | Refresh | Parts |
|---|---|---|---|---|
| Direct GPIO (**v1**) | 3 | yes | no | none |
| Charlieplex | 6 | multiplexed | **yes** | none |
| 74HC138 decoder | 8 | **one at a time** | yes, if >1 | 1 IC |
| **74HC595 shift register** | **8 → 16 → 24 cascaded** | **yes** | **no** | 1+ IC |
| APA102 / SK9822 (2 pins) | unlimited RGB | yes | no | the LEDs |

**A decoder gives 8 one-hot states, not 8 controllable LEDs.** A 74HC138 asserts
exactly one output at a time, so displaying two at once means multiplexing — which
brings back the continuous refresh loop. Fine if the indicators are genuinely
mutually exclusive (a mode display); wrong if they are independent flags.

**Prefer the 74HC595.** Same three pins (data, clock, latch), but 8 *independent*
outputs that **latch and stay put** — no refresh, so the MCU can sleep, the main
loop carries no timing obligation, and a long I²C transaction cannot glitch the
display. Cascade a second device for 16, a third for 24, with no extra pins. This
is the same principle that picks APA102 over WS2812: **on a bus slave, prefer a
latching driver over a multiplexed one.**

Give each 595 output its own series resistor (it is a logic output, not a
constant-current driver); use TPIC6B595 if you ever need real current.

### Dimming a shift register

The 595's **`OE`** pin would give clean global PWM by blanking all outputs, but it
is a *fourth* pin and SER/SRCLK/RCLK already take all three. Tie `OE` low and dim
by **re-clocking the register** instead — write the pattern, write the gated
pattern, repeat.

The honest consequence: **dimming forfeits the set-and-forget property** that made
the 595 attractive. Two things keep that acceptable:

- **It is cheap.** A 595 write is ~25 GPIO operations ≈ 10 µs; 16 PWM phases per
  frame at 100 Hz is 160 µs per 10 ms — about **1.6 % CPU**. Nothing like WS2812.
- **Per-LED dimming costs exactly the same as global**, since the register must be
  re-clocked either way. So there is no reason to settle for a global-only knob.

`led_core_is_static()` reports when nothing needs animating — every lit LED at
full level, master brightness full, nothing blinking. **A driver should write once
and stop refreshing while that holds**, which restores set-and-forget (and lets
the MCU sleep) whenever dimming is not actually in use.

### What this costs above the driver: nothing

The host reads `LED_COUNT` and writes a `LED_STATE` bitmask — it never learns how
the LEDs are wired. `LED_CORE_MAX` is already 8, matching the byte width of
`LED_STATE`, so **moving from 3 GPIO to 8 via a 595 needs no change to the
register map, the host, or the tests** — only `LED_COUNT` reports a different
number. Past 8, add `LED_STATE` extension bytes; block writes already
auto-increment.

(For a *pure* production LED board, a dedicated I²C driver — PCA9685, TLC59108,
IS31FL3731 — beats an ATtiny85 outright, since it needs no firmware at all. The
'85 version earns its place only as the reference slave.)

## Schematic

```
                    ATtiny85
                  ┌────∪────┐
      RESET ──────┤1 PB5    │
     (LED1) PB3 ──┤2      PB2├── SCL ──┬──────────► Qwiic SCL
     (LED2) PB4 ──┤3      PB1├── LED0  │
        GND ──────┤4      PB0├── SDA ──┼──┬───────► Qwiic SDA
                  └─────────┘          │  │
                     │  8 = VCC        │  │   NO pull-ups on this board —
                     │                 │  │   the master owns them.
   VCC ─┬──[100nF]──GND                │  │
        └──[ 10µF]──GND   (decoupling, close to pin 8)

   PB1 ──[1kΩ]──▶|── GND      LED0   red/amber, Vf ≈ 1.8–2.1 V
   PB3 ──[1kΩ]──▶|── GND      LED1
   PB4 ──[1kΩ]──▶|── GND      LED2
```

## Design notes

- **1 kΩ series resistors work at both rails** — ≈1.5 mA at 3.3 V, ≈3.2 mA at
  5 V. Both are plainly visible and both are far inside the ATtiny's 20 mA/pin
  comfort zone. One value, no respin when the bench moves from 5 V to 3.3 V.
- **Use red or amber LEDs.** At 3.3 V, blue/white/green (Vf ≈ 3.0–3.4 V) barely
  light. This bites people who prototype at 5 V and then migrate.
- **No SDA/SCL pull-ups on this board.** Exactly one set lives on the master.
  Parallel pull-ups across several modules drag the bus down until it stops
  working.
- **1–2 mA is not a compromise.** Perceived brightness is logarithmic, and
  average current is what drains the LiPo — see architecture §9.5.
- Two **Qwiic / STEMMA QT** connectors (JST-SH 4-pin) wired in parallel so the
  bus passes through.

## ⚠️ ISP pins collide with the bus

In-system programming uses **MOSI = PB0, MISO = PB1, SCK = PB2, RESET = PB5** —
which are the same pins as SDA, LED0 and SCL. Consequences:

- **Program the '85 before connecting it to the bus**, or disconnect the bus
  while programming. Other devices on SDA/SCL will otherwise fight the programmer.
- LED0 flickering during programming is expected, not a fault.
- A ZIF socket or a 6-pin ISP header makes the reflash loop far less painful
  while iterating. (This whole class of annoyance is what UPDI on a tinyAVR-1
  removes.)

## BOM

| Qty | Part | Notes |
|---|---|---|
| 1 | ATtiny85 (DIP-8 for the PoC, SOIC-8 for a PCB) | 8 MHz internal RC |
| 3 | LED, red or amber, 3 mm/5 mm | low Vf — required at 3.3 V |
| 3 | 1 kΩ resistor | series, works at 3.3 V and 5 V |
| 1 | 100 nF ceramic | decoupling, close to VCC |
| 1 | 10 µF | local bulk |
| 2 | JST-SH 4-pin (Qwiic) | bus pass-through |
| 1 | 6-pin ISP header | optional but recommended |

Not on this board: bus pull-ups (master), power regulation (`smartiny-pwr`).
