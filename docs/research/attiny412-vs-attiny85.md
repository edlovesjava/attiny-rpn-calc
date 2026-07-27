# Research — ATtiny412 vs ATtiny85 for smartiny modules

Evaluating a switch from the ATtiny85 to the ATtiny412 (tinyAVR 1-series) as the
standard smartiny slave brain.

> **Verdict: stay on the '85 for the breadboard/perfboard phase; revisit at PCB
> time.** The 412's wins are real but none of them unblock current work, the pin
> budget is unchanged, and it is a *memory downgrade*. Crucially the port is cheap
> whenever we want it, so this is not a now-or-never decision.

## The headline surprise: less memory, not more

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

## Pin budget: exactly a wash

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

## What genuinely improves

### 1. Hardware TWI instead of USI — the big one

USI is not an I²C peripheral; it is a shift register with I²C built on top in
software. Nearly every awkward constraint in this project traces back to it:

- "USI must stay the only time-critical handler" (§9.6)
- WS2812 ruled out because interrupts-off corrupts a transaction
- soft PWM confined to the main loop
- clock stretching handled by hand

Real TWI gives byte-level interrupts, **hardware address matching** (plus a
second address and address masking), and automatic clock stretching — and it keeps
working while the CPU does something else. Most of that anxiety simply goes away.

### 2. UPDI does not collide with the bus

On the '85, ISP uses MOSI=PB0, MISO=PB1, SCK=PB2 — which *are* SDA, LED0 and SCL.
Reprogramming means unplugging the module from the bus, and the programming jig
needs a jumper block to isolate the ISP lines.

**UPDI is one dedicated pin (PA0).** Reflash in-circuit, bus live, nothing
disconnected. For an ecosystem where modules get reflashed constantly this is the
biggest day-to-day quality-of-life gain — and it collapses the bench rig too: a
USB-serial adapter plus one resistor (SerialUPDI), with no ArduinoISP sketch, no
10 µF reset-capacitor gotcha, and no 2×3 isolation jumper block.

### 3. Sampled BOD — closes an open architecture problem

Architecture §9.6 records an unresolved tradeoff: the '85's brown-out detector
costs **~20 µA continuously**, which dominates a µA sleep budget, and the '85
cannot disable it automatically on sleep. tinyAVR-1 offers **sampled BOD** modes
that run the detector intermittently for roughly ~1 µA.

That is not a nicety — it *resolves* a documented open issue, and matters directly
to the ~500 h battery target.

### 4. ADC sample accumulation in hardware

The tinyAVR-1 ADC can accumulate up to 64 samples itself (`SAMPNUM`), replacing
the software averaging in `ladder_test.ino` at zero CPU cost.

*Possible* bonus: with ≥1 LSB of noise present, accumulation gives genuine
oversampling resolution, which would widen the ladder's 6–14 count margins. Not
guaranteed — a perfectly quiet DC signal just returns N copies of the same value —
so treat it as upside, not a reason to switch.

## Costs and risks

- **SOIC-8 only.** Needs a SOIC→DIP adapter for breadboard and perfboard work.
  1.27 mm pitch is comfortably hand-solderable with a fine tip and flux, and the
  adapters cost pennies — but it is a step that does not exist today, at exactly
  the stage where iteration speed matters most.
- **256 B SRAM** — see above.
- **Two toolchains** if modules end up mixed: ATTinyCore/ISP for '85s, megaTinyCore/
  UPDI for 412s. Tolerable, but it doubles the bench setup docs.
- Fuse/clock setup differs (no `CKDIV8` ritual, but new conventions to learn).

## What the port would actually cost

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

## If you are going to solder SOIC anyway…

The sharpest counter-argument to the 412 specifically: **it costs SOIC work and
gives back less memory.** If the SOIC step is being accepted regardless, an
**ATtiny1614** (SOIC-14) brings 16 KB flash, **2 KB SRAM** and 12 I/O for the same
soldering effort — which would relieve the pin scarcity *and* the memory ceiling
rather than trading one for the other.

That does break the eight-pin minimalism the project is partly *about*, so it is a
question of what the constraint is for. The 412 keeps the aesthetic; the 1614
keeps the aesthetic's cost from biting.

## Recommendation

1. **Keep the '85 through breadboard and perfboard.** DIP-8 is the right package
   for this phase, the design is complete, and nothing the 412 offers unblocks it.
2. **Revisit at PCB layout**, when the part is being soldered SMD anyway and the
   SOIC objection evaporates. Sampled BOD and hardware TWI are worth having in a
   board meant to run on a LiPo.
3. **Separately: the motherboard is a different question.** `smartiny-calc` needs
   more than the '85 (SSD1306 framebuffer RAM, a real USART for the USB path) —
   but the 412 is the *wrong* tinyAVR for it. That points at a 1614/1616. See
   architecture §11 decision 5.
4. **Keep `core/` hardware-free.** It is what makes all of the above optional.

## Sources

- [ATtiny412 product page](https://www.microchip.com/en-us/product/attiny412) (Microchip)
- [ATtiny212/412 datasheet DS40001911B](https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny212-412-DataSheet-DS40001911B.pdf)
- [ATtiny212/214/412/414/416 datasheet DS40002287A](https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny212-214-412-414-416-DataSheet-DS40002287A.pdf)

> Pin assignments (TWI on PA1/PA2, ADC channels on PA3/PA6/PA7) are from the
> 8-pin tinyAVR-1 standard mapping — **verify against the datasheet before
> committing a layout**, since alternate mappings exist on some family members.
