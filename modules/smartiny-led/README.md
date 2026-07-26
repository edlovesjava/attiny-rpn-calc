# smartiny-led

Indicator / light output. **Addr `0x21` · `WHO_AM_I` `0x02` · ATtiny85 slave**

**This is Board 0 — the reference slave.** It is built first not because the
product needs it first (the OLED can show status), but because it is the
simplest *complete* vertical slice of the architecture: a full I²C slave — common
register header, TinyWireS glue, EEPROM-persisted address, Qwiic footprint,
pull-up and power rules — with trivial application logic. Get the bus plumbing
right here, on easy mode, and **every other module forks this skeleton**.

## Contents

```
hardware/README.md      pin budget, schematic, BOM, the ISP/bus pin collision
docs/build-plan.md      staged PoC: laptop tests → blink → PC master → bus → '85 master
firmware/core/          led_core.c — LED state + soft PWM, pure C, no hardware
firmware/smartiny_led/  the .ino: transport in, pins out, nothing clever
tests/                  host-side unit tests — `make` (no AVR toolchain needed)
```

```console
$ cd tests && make
31 checks, 0 failures
```

## v1: 3 LEDs, dead simple

The ATtiny85 has 6 I/O; PB5 is RESET and USI takes PB0/PB2, leaving **3 free
GPIO → 3 direct-drive LEDs** on PB1/PB3/PB4. No charlieplexing, no decoder, no
refresh loop. (The old "4 LEDs on 2 GPIO" problem only existed when the LEDs
shared the *keypad's* chip; giving them their own '85 dissolved it — the 4th LED
is now just a pin-budget fact, not a puzzle.)

A 4th needs either charlieplexing the same 3 pins (up to 6 LEDs, but the refresh
loop comes back) or APA102 on 2 pins — see `hardware/README.md`.

At 3.3 V use **red/amber LEDs** (Vf ≈ 1.8–2.1 V); blue/white/green (Vf ≈ 3.0–3.4 V)
barely light. 1 kΩ series resistors work unchanged at 3.3 V and 5 V.

## Registers

| Reg | Name | Access | Meaning |
|---|---|---|---|
| `0x00–0x04` | common header | | `WHO_AM_I`=`0x02`, `VERSION`, `STATUS`, `CONFIG`, `I2C_ADDR` |
| `0x10` | `LED_COUNT` | R | LEDs populated (3) |
| `0x11` | `LED_STATE` | R/W | on/off bitmask, masked to populated |
| `0x12` | `LED_BRIGHTNESS` | R/W | global soft-PWM, 0–255 |
| `0x13` | `LED_BLINK` | R/W | bitmask: which LEDs blink rather than sit steady |
| `0x14` | `LED_BLINK_MS` | R/W | blink period, ×10 ms (0 = no gating) |
| `0x15` | `LED_BLINK_DUTY` | R/W | on-fraction of the period, 0–255 |
| `0x18…` | `LEDn_RGB` | — | v2 (APA102) |
| `0x1F` | `PATTERN` | — | v2 (smart firmware) |

An LED lights when its `LED_STATE` bit is set and — if its `LED_BLINK` bit is also
set — the blink gate is open. **Blinking lives in the module, not the host**, so
the host is not obliged to hold a timer (and bus traffic) just to animate an
indicator. One shared rate and duty covers "these two blink, that one is solid";
per-LED rates and canned patterns are v2, if ever.

```console
$ i2cset -y 3 0x21 0x11 0x07   # all three on
$ i2cset -y 3 0x21 0x13 0x02   # LED1 blinks, LED0 and LED2 stay steady
$ i2cset -y 3 0x21 0x14 25     # 250 ms period
$ i2cset -y 3 0x21 0x12 0x30   # dim them all
```

Definitions: [`lib/smartiny-common/smartiny_regs.h`](../../lib/smartiny-common/smartiny_regs.h).

## Firmware rules worth inheriting

- **Soft PWM runs in the main loop, never an ISR.** USI must stay the only
  time-critical handler (architecture §9.6).
- **Drive LED pins with single-bit ops only.** `PORTB |= (1<<n)` compiles to the
  atomic SBI instruction; a read-modify-write of the whole port would race the
  USI ISR, which also touches PORTB for SDA/SCL.
- **A blank EEPROM reads `0xFF`**, which is an illegal I²C address, so an
  unprogrammed board falls back to the catalogue default automatically.
- **Reject reserved addresses** (`<0x08`, `>0x77`) — a bad `I2C_ADDR` write would
  otherwise strand the module at an unreachable address.

## v2 and beyond

- **APA102 / SK9822** addressable RGB: clocked protocol, so it can be bit-banged
  slowly *without* disabling interrupts — it never fights USI I²C timing the way
  WS2812's strict 800 kHz does. They latch their own state, so the MCU can sleep,
  and PB3 frees up for the shared `INT` line.
- Drive current is a *power* decision: 1–2 mA is plainly visible, and PWM dimming
  buys large savings for little perceived loss. See architecture §9.5.
- **Never run strip current through the Qwiic `VCC` line** — inject at the strip.

## Status

- ✅ Register model, slave engine, LED core + host tests
- ✅ Firmware sketch, hardware design, staged build plan
- ⬜ Stage 1+ — flash a real '85 and work down `docs/build-plan.md`
