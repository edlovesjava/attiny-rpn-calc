# Research — chip strategy for smartiny modules

Evaluating whether to move off the ATtiny85, and what to standardise on longer
term.

> **Verdict: a two-tier standard.** Keep 8-pin parts for slaves — that constraint
> is the point, and the '85 is the best 8-pin AVR there is. Adopt the 14-pin
> **ATtiny1624** (tinyAVR 2) as the "capable" tier for the host and anything that
> genuinely outgrows 8 pins. The `core`/`platform` split means both tiers run the
> same code.
>
> Near term: **stay on the '85** through breadboard and perfboard. Nothing on
> offer unblocks current work, and the port is cheap whenever we want it.

## The constraint that settles it: no 8-pin tinyAVR 2

The tinyAVR 2-series **starts at 14 pins**. There is no 8-pin part in the family,
so "8-pin" and "2-series" are mutually exclusive — the choice is binary, not a
spectrum.

That makes a second fact decisive:

> **The ATtiny85 is the most capable 8-pin AVR available.** 8 KB flash and 512 B
> SRAM beat every modern 8-pin option — the 1-series tops out at the ATtiny412's
> 4 KB / 256 B. Choosing a modern 8-pin part means *paying* memory for peripherals.

So if the eight-pin footprint is the identity, the '85 is not a legacy compromise
— it is the correct part, and will stay so unless Microchip ships an 8-pin 2-series.

## The three candidates

| | ATtiny85 | ATtiny412 | **ATtiny1624** |
|---|---|---|---|
| Series | classic (0) | tinyAVR 1 | **tinyAVR 2** |
| Pins | **8 — DIP + SOIC** | 8 — SOIC | 14 — SOIC |
| Usable I/O | 5 | 5 | **11** |
| Flash | 8 KB | 4 KB | **16 KB** |
| SRAM | 512 B | 256 B | **2 KB** |
| EEPROM | 512 B | 128 B | 256 B |
| ADC | 10-bit | 10-bit | **12-bit differential + PGA** |
| I²C | USI (software) | TWI | TWI |
| Programming | ISP — *collides with the bus* | UPDI | UPDI |
| BOD | ~20 µA always on | sampled | sampled |

## The irony worth naming

The ATtiny1624's **12-bit ADC would roughly quadruple the ladder's margins** —
the 14-count minimum gap becomes ~56, and the worst-case 6-count margin becomes
~24. Tolerance stops mattering and EEPROM self-calibration becomes unnecessary.

But **the 8-pin constraint is the entire reason the ladder exists.** Give the
keypad 11 I/O and the obvious move is an ordinary 8-GPIO matrix scan — the base-4
trick, the wake-and-decode-on-one-wire result, the collision analysis, all of it
becomes unnecessary rather than better.

> The chip that would most improve the ladder is the chip that makes the ladder
> pointless.

So this is not really a performance decision. **Keep the keypad on 8 pins**: that
is where the aesthetic and the engineering interest coincide, and it is what makes
the module worth building at all.

*(Escape hatch: if bench measurement shows the margins genuinely do not hold, a
1624 running the **same ladder** at 12-bit rescues the design rather than
replacing it — you keep the passive matrix, the pinout, and the decode.)*

## Where more pins genuinely earn their place

**`smartiny-calc`, the host.** Its needs are different in kind, not degree:

- an SSD1306 framebuffer is 1 KB — the '85 has 512 B total, so it *cannot* hold one
- the USB path wants a real USART (§11 decision 9), which the '85 lacks
- it masters the bus, formats the display, and runs the RPN stack

2 KB SRAM, 16 KB flash and 11 I/O answer all three. This is the seat where the
8-pin constraint stops being interesting and starts being an obstacle.

## Proposed standard

| Tier | Part | For |
|---|---|---|
| **mini** — 8-pin | **ATtiny85** (DIP for bench, SOIC for PCB) | slaves: `smartiny-key`, `smartiny-led`, `smartiny-pwr` |
| **max** — 14-pin | **ATtiny1624** (tinyAVR 2) | `smartiny-calc`, and any module that truly outgrows 8 pins |

Two footprints, **one codebase**: `smartiny_regs.h`, the slave engine and every
`*_core.c` are hardware-free and unchanged across both. Only the per-module
platform glue differs — which is exactly what §4 was for.

Accept the cost honestly: two toolchains on the bench (ATTinyCore + ISP for the
'85s, megaTinyCore + UPDI for the 1624). That is one extra USB-serial adapter and
a second page in `bench-setup.md`.

## Notes on the ATtiny412 specifically

The 8-pin modern option, examined in detail — and rejected as a *replacement* for
the '85, though its peripherals are what the 1624 also brings.

### Less memory, not more

| | ATtiny85 | ATtiny412 | |
|---|---|---|---|
| Flash | **8 KB** | 4 KB | ½ |
| SRAM | **512 B** | 256 B | ½ |
| EEPROM | **512 B** | 128 B | ¼ |
| Package | **DIP-8** + SOIC | SOIC-8 / VQFN only | |
| Max clock | 20 MHz (8 int.) | 20 MHz internal | |

It reads like an upgrade because the part number is higher and the peripherals are
newer, but on memory the 412 is *half an '85*. Worth sizing before committing:

- Our structures are small — slave engine ~10 B, `led_core` ~15 B, a future
  `key_core` with a 16-event FIFO ~40 B.
- The framework is not: `Wire` alone typically carries 32-byte TX and RX buffers.
- Add stack and 256 B gets tight — workable, but with little headroom.

Partly offset: hardware TWI needs far less flash than bit-banged TinyWireS, and
megaTinyCore is leaner than classic ATTinyCore. So the *effective* gap is under
2×. Still, **256 B SRAM is the main risk of this switch**, and it is the opposite
of what "upgrade" suggests.

EEPROM at 128 B is a non-issue — our whole config is a dozen bytes.

### Pin budget: exactly a wash

This is the disappointment. The 412 does **not** relieve the pin scarcity that
shaped so many decisions (one status LED, `INT` vs a second LED, no debug UART).

| | ATtiny85 | ATtiny412 |
|---|---|---|
| Total I/O | 6 | 6 |
| Reserved for programming | PB5 (RESET) | PA0 (UPDI) |
| **Usable** | **5** | **5** |
| I²C | PB0 SDA / PB2 SCL (USI) | PA1 SDA / PA2 SCL (TWI) |
| **Free after I²C** | **3** — PB1, PB3, PB4 | **3** — PA3, PA6, PA7 |

Small bonus: on the 412 **all three free pins are ADC-capable**, where the '85
constrained `SENSE` to PB3/PB4. More layout freedom, no extra capability.

### What genuinely improves

#### 1. Hardware TWI instead of USI — the big one

USI is not an I²C peripheral; it is a shift register with I²C built on top in
software. Nearly every awkward constraint in this project traces back to it:

- "USI must stay the only time-critical handler" (§9.6)
- WS2812 ruled out because interrupts-off corrupts a transaction
- soft PWM confined to the main loop
- clock stretching handled by hand

Real TWI gives byte-level interrupts, **hardware address matching** (plus a
second address and address masking), and automatic clock stretching — and it keeps
working while the CPU does something else. Most of that anxiety simply goes away.

#### 2. UPDI does not collide with the bus

On the '85, ISP uses MOSI=PB0, MISO=PB1, SCK=PB2 — which *are* SDA, LED0 and SCL.
Reprogramming means unplugging the module from the bus, and the programming jig
needs a jumper block to isolate the ISP lines.

**UPDI is one dedicated pin (PA0).** Reflash in-circuit, bus live, nothing
disconnected. For an ecosystem where modules get reflashed constantly this is the
biggest day-to-day quality-of-life gain — and it collapses the bench rig too: a
USB-serial adapter plus one resistor (SerialUPDI), with no ArduinoISP sketch, no
10 µF reset-capacitor gotcha, and no 2×3 isolation jumper block.

#### 3. Sampled BOD — closes an open architecture problem

Architecture §9.6 records an unresolved tradeoff: the '85's brown-out detector
costs **~20 µA continuously**, which dominates a µA sleep budget, and the '85
cannot disable it automatically on sleep. tinyAVR-1 offers **sampled BOD** modes
that run the detector intermittently for roughly ~1 µA.

That is not a nicety — it *resolves* a documented open issue, and matters directly
to the ~500 h battery target.

#### 4. ADC sample accumulation in hardware

The tinyAVR-1 ADC can accumulate up to 64 samples itself (`SAMPNUM`), replacing
the software averaging in `ladder_test.ino` at zero CPU cost.

*Possible* bonus: with ≥1 LSB of noise present, accumulation gives genuine
oversampling resolution, which would widen the ladder's 6–14 count margins. Not
guaranteed — a perfectly quiet DC signal just returns N copies of the same value —
so treat it as upside, not a reason to switch.

### Costs and risks

- **SOIC-8 only.** Needs a SOIC→DIP adapter for breadboard and perfboard work.
  1.27 mm pitch is comfortably hand-solderable with a fine tip and flux, and the
  adapters cost pennies — but it is a step that does not exist today, at exactly
  the stage where iteration speed matters most.
- **256 B SRAM** — see above.
- **Two toolchains** if modules end up mixed: ATTinyCore/ISP for '85s, megaTinyCore/
  UPDI for 412s. Tolerable, but it doubles the bench setup docs.
- Fuse/clock setup differs (no `CKDIV8` ritual, but new conventions to learn).

## What a port would actually cost

Very little, and this is the reassuring part — it is exactly what the
`core` / `platform` split in §4 was for:

| Layer | Port impact |
|---|---|
| `smartiny_regs.h` | **none** |
| `smartiny-slave/` | **none** — hardware-free by construction |
| `led_core.c`, future `key_core.c` | **none** |
| host tests (54 checks) | **none** — still run on the laptop |
| the `.ino` platform glue | TinyWireS → `Wire`, `PORTB` → `PORTA`, EEPROM API |

The port is a rewrite of one small file per module. If the architecture is doing
its job, that is all a chip change should cost — and it means we can defer the
decision without accumulating debt.

### If you are going to solder SOIC anyway…

The sharpest counter-argument to the 412 specifically: **it costs SOIC work and
gives back less memory.** If the SOIC step is being accepted regardless, an
**ATtiny1614** (SOIC-14) brings 16 KB flash, **2 KB SRAM** and 12 I/O for the same
soldering effort — which would relieve the pin scarcity *and* the memory ceiling
rather than trading one for the other.

That does break the eight-pin minimalism the project is partly *about*, so it is a
question of what the constraint is for. The 412 keeps the aesthetic; the 1614
keeps the aesthetic's cost from biting.

## Recommendation

1. **Keep the '85 through breadboard and perfboard.** DIP-8 suits this phase, the
   design is complete, and nothing on offer unblocks it.
2. **Adopt the two-tier standard** above as the long-term shape: 8-pin slaves,
   ATtiny1624 host.
3. **Keep the keypad on 8 pins permanently** — the constraint is the design.
4. **Keep `core/` hardware-free.** It is what makes every one of these decisions
   reversible.

## Sources

- [ATtiny412 product page](https://www.microchip.com/en-us/product/attiny412) (Microchip)
- [ATtiny212/412 datasheet DS40001911B](https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny212-412-DataSheet-DS40001911B.pdf)
- [ATtiny212/214/412/414/416 datasheet DS40002287A](https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny212-214-412-414-416-DataSheet-DS40002287A.pdf)
- [ATtiny1624/1626/1627 datasheet DS40002234A](https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny1624-26-27-DataSheet-DS40002234A.pdf) (tinyAVR 2)
- [ATtiny3224/3226/3227 datasheet DS40002345A](https://www.microchip.com/content/dam/mchp/documents/MCU08/ProductDocuments/DataSheets/ATtiny3224-3226-3227-Data-Sheet-DS40002345A.pdf)
- [megaTinyCore ATtiny_x24 notes](https://github.com/SpenceKonde/megaTinyCore/blob/master/megaavr/extras/ATtiny_x24.md) — the 2-series 8-pin gap is noted here too

> Pin assignments (TWI on PA1/PA2, ADC channels on PA3/PA6/PA7) are from the
> 8-pin tinyAVR-1 standard mapping — **verify against the datasheet before
> committing a layout**, since alternate mappings exist on some family members.
