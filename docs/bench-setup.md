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
| 0.1" breakaway male header | strip | keypad's 8-pin connector, ISP headers |
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
