# Bench setup — shared tooling and consumables

Per-board parts live in each module's BOM
([keypad](../modules/smartiny-key/docs/ladder.md#bom-per-keypad),
[LED](../modules/smartiny-led/hardware/README.md#bom)). This page covers what
spans modules: the tools and the consumables you burn through.

## Tools

| Item | Why | Note |
|---|---|---|
| **ISP programmer** (USBasp) | **The only way to flash an ATtiny85.** | A CP2112/MCP2221A is an *I²C* bridge — it cannot program a chip. Easy and costly to conflate. |
| **Arduino Uno/Nano** | Runs the ladder bench sketch; doubles as ISP | "Arduino as ISP" works if you'd rather not buy a USBasp |
| **CP2112 dongle** | PC acts as I²C master to exercise a slave before any host board exists | Driverless; on Linux `i2c-cp2112` gives `/dev/i2c-*`, so `i2cdetect`/`i2cget`/`i2cset` just work |
| **8-ch USB logic analyzer** (~$10) | Debugging a bit-banged USI I²C slave | Highest-value tool after the programmer. With PulseView/sigrok's I²C decoder, "it doesn't respond" becomes "NAK on byte 2". Debugging this blind is miserable. |
| **Multimeter with continuity** | Buzzing out keypad pinouts, verifying ladder values | Cheap modules mislabel row/col order |
| **Breadboard 3.3 V supply** | Testing at the real target rail, not just 5 V | MB102-style module, or an Uno's 3.3 V pin (µA-scale loads here) |

## Consumables

| Item | Qty | For |
|---|---|---|
| ATtiny85-20PU (DIP-8) | 5–10 | one per module + spares; SOIC-8 later for PCBs |
| DIP-8 sockets | 10 | ISP shares pins with the bus, so chips come in and out a lot |
| 100 nF ceramic | 50 | decoupling — one per chip, always |
| 10 µF | 20 | local bulk |
| 10 nF | 20 | `Csense` on the keypad ladder |
| 6 mm SPST-NO tactile switches | 50 | 16 per keypad; they cost pennies and they ping away |
| Red / amber LEDs | 20 | **required at 3.3 V** — blue/white/green (Vf ≈ 3.0–3.4 V) barely light |
| Perfboard | few | keypad matrix, then module prototypes |
| 0.1" breakaway male header | strip | keypad's 8-pin connector, `SMARTINY-6` headers |
| Bare tinned + insulated hookup wire | — | perfboard matrix: one axis bare, one insulated |
| 1 % resistors | have | ladder: 5.6k, 11k, 16k, 1.1k, 2.7k, 3.9k, 39k · pull-ups: 4.7k |

## Using the logic analyzer

An 8-channel 24 MHz FX2LA-style analyzer is plenty: 24 MHz against 100 kHz I²C is
240× oversampling.

- **Sample at 1–2 MHz, not 24.** Still ~10–20× oversampled, and it avoids the USB
  buffer overruns that make these clones feel unreliable on long captures.
- **CH0 → SCL, CH1 → SDA, plus a ground clip to target GND.** A floating ground
  is the most common cause of garbage edges.
- **PulseView (sigrok) + the I²C decoder** annotates start/stop, address, R/W,
  ACK/NAK and data inline — this is what turns "it doesn't respond" into "address
  ACKed, NAK on byte 2".
- **It cannot see the keypad ladder.** `SENSE` is analog; a logic analyzer only
  reports above/below a threshold. Ladder validation stays with the serial output
  of `ladder_test.ino` plus a multimeter. The analyzer is for the *bus*.
- **Borrow an LED pin as a trace probe.** The '85 has no spare GPIO, but during
  bring-up LED0 (PB1) can be toggled high on USI ISR entry and low on exit, with
  CH2 watching it. That measures ISR duration against live I²C traffic — the way
  to *verify* the "USI is the only time-critical handler" rule rather than assume it.

## Bench builds — todo

### 1. The smartiny dock — programmer *and* bus master ⬜

The upgrade worth building rather than a plain programmer. A stock USBasp only
flashes; the tedious part of the loop is **flash → talk to it over I²C → repeat**,
which normally means moving wires every cycle.

Because `SMARTINY-6` puts ISP and I²C on the same pins (architecture §3), one dock
can do both — if it can switch roles:

1. **Program mode** — drive `RESET`/`MOSI`/`MISO`/`SCK`, flash the target.
2. **Release** — tri-state the SPI lines.
3. **Bus mode** — become an I²C master on the *same* two wires and exercise the
   module's registers.

**A single Arduino Nano does all of it.** It has SPI (for `ArduinoISP`) and TWI
(for `Wire`), and `SPI.end()` releases the SPI pins so `Wire` can take the same
target lines:

| Nano | → | `SMARTINY-6` | Target ('85) |
|---|---|---|---|
| D11 MOSI **+** A4 SDA | → | pin 3 | PB0 |
| D13 SCK **+** A5 SCL | → | pin 4 | PB2 |
| D12 MISO | → | pin 6 | PB1 |
| D10 | → | pin 5 | RESET |
| 5V / 3V3 | → | pin 2 | VCC |
| GND | → | pin 1 | GND |

> **Put ~330 Ω in series on D11 and D13.** Two Nano pins share each target line
> and only one role is active at a time; the resistors make any moment of overlap
> harmless instead of a short. Cheap insurance for a mode switch that will
> occasionally be got wrong.

**Also build in**

- **ZIF socket** for bare DIP-8 chips, **plus** a `SMARTINY-6` cable for assembled
  modules — the two ways a chip arrives.
- **VCC select, 3.3 V / 5 V.** Our target rail is 3.3 V but ISP is habitually 5 V;
  making it a switch stops the "one voltage across the whole bench" rule from
  being violated by accident.
- **Status LEDs** on D9/D8/D7 — `ArduinoISP` already drives heartbeat, error and
  programming.
- A conventional **2×3 AVR ISP** connector too, so a stock USBasp can drive the
  same target.

Flash the Nano with the stock **ArduinoISP** example to start; the mode-switching
firmware is a later refinement.

> ⚠️ **10 µF between the Nano's own RESET and GND** (+ to RESET). Without it the
> Nano auto-resets when `avrdude` opens the serial port and programming fails with
> a sync error. This is *the* Arduino-as-ISP gotcha.
>
> The cap must be **absent** while uploading a sketch *to* the Nano and **present**
> while programming a target — so put it on a jumper or slide switch. Designing
> that in is most of the jig's value.

### 1a. Minimal fallback — plain Nano-as-ISP ⬜

If the dock is more than you want today, the classic wiring still works and is
worth soldering to perfboard rather than re-breadboarding each time — because ISP
shares pins with the I²C bus, chips come out of circuit constantly.

| Nano | → | ATtiny85 (DIP-8) | |
|---|---|---|---|
| D10 | → | pin 1 | `PB5` / RESET |
| D11 | → | pin 5 | `PB0` / MOSI |
| D12 | → | pin 6 | `PB1` / MISO |
| D13 | → | pin 7 | `PB2` / SCK |
| 5V | → | pin 8 | VCC |
| GND | → | pin 4 | GND |

Same 10 µF RESET-capacitor caveat as above.

**Worth building in**

- **ZIF socket** (or at minimum a DIP-8 socket) — the whole point is fast swaps.
- **Status LEDs** on D9 / D8 / D7 — the ArduinoISP sketch already drives
  heartbeat / error / programming.
- **All 8 target pins broken out to a header**, so a socketed chip can be
  bench-tested in place. Break the ISP lines (`PB0`/`PB1`/`PB2`) with a **2×3
  jumper block** so they can be lifted when the chip is running I²C, since those
  pins are SDA / LED0 / SCL:

  ```
          Nano          ZIF socket
   MOSI ──o     o── pin 5 (PB0/SDA)
   MISO ──o     o── pin 6 (PB1/LED0)
    SCK ──o     o── pin 7 (PB2/SCL)
           ↑ shunts bridge the rows
  ```

  A "jumper block" is just two male header pins bridged by a removable shunt —
  shunts on for programming, off to isolate the target. A female-to-female dupont
  wire substitutes fine if you have no shunts.
- **A 6-pin ISP header output**, so the jig can also program a finished module
  in-circuit.

**After building, verify with the fuse step this project needs**

ATtiny85s ship at 1 MHz — 8 MHz internal RC divided by the `CKDIV8` fuse. Select
**ATtiny85 / 8 MHz internal** in ATTinyCore and run **Burn Bootloader**, which on
a tiny does not burn a bootloader at all: it just writes the fuses and clears
`CKDIV8`. Skip it and everything runs 8× slow — most visibly, USI I²C timing
misses.

## Bus and later modules

- **SSD1306 0.96" I²C OLED ×2** — build-plan Stage 4 (two devices on one bus), and
  the calculator display eventually. `0x3C`.
- **Qwiic / STEMMA QT cables + a JST-SH breakout** — once you move off jumpers.
- **TP4056 (with DW01), small LiPo, MCP1700-3.3** — `smartiny-pwr` in three parts.
- **FM24CL64 FRAM** — `smartiny-mem`; instant writes, no page delay, huge endurance.

## Bench rules worth re-reading before powering on

- **One voltage across the whole bench.** A 5 V CP2112 with a 3.3 V-powered
  ATtiny is out of spec — AVR pins tolerate only VCC+0.5 V. Match, don't mix.
- **Exactly one set of bus pull-ups**, on the master. 4.7 kΩ at 100 kHz.
- **Program before connecting to the bus.** ISP uses MOSI=PB0, MISO=PB1, SCK=PB2 —
  the same pins as SDA, LED0 and SCL.
- **Run I²C bridges in I²C mode, not strict SMBus** — SMBus's ~35 ms
  clock-stretch timeout can trip a slave that stretches while waking.
