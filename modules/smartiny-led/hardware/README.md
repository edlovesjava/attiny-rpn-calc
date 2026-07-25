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

That leaves **3 free GPIO → 3 direct-drive LEDs**. A 4th needs one of:

- **Charlieplex** the same 3 pins → up to 6 LEDs, but reintroduces a continuous
  refresh loop competing with USI timing.
- **APA102 / SK9822** on 2 pins (PB1 data, PB4 clock) → any number of RGB LEDs,
  fire-and-forget with no refresh, and PB3 stays free for the shared `INT` line.

**v1 populates 3 and stays dumb.** The point of Board 0 is the slave skeleton,
not the light show.

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
