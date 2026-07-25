# smartiny-led

Indicator / light output. **Addr `0x21` · `WHO_AM_I` `0x02` · ATtiny85 slave**

**This is Board 0 — the reference slave.** It is built first not because the
product needs it first (the OLED can show status), but because it is the
simplest *complete* vertical slice of the architecture: a full I²C slave — common
register header, TinyWireS glue, EEPROM-persisted address, Qwiic footprint,
pull-up and power rules — with trivial application logic. Get the bus plumbing
right here, on easy mode, and **every other module forks this skeleton**.

## v1: keep it dead simple

A dedicated ATtiny85 has PB1/PB3/PB4 free once I²C takes PB0/PB2, so **3–4
direct-drive GPIO LEDs need no cleverness at all** — no charlieplexing, no
decoder. (The "4 LEDs on 2 GPIO" problem only existed when the LEDs shared the
keypad's chip; moving the boundary dissolved it.)

At 3.3 V use **red/amber LEDs** (Vf ≈ 1.8–2.1 V) — blue/white/green (Vf ≈ 3.0–3.4 V)
barely light.

## v2 and beyond

- **APA102 / SK9822** addressable RGB: clocked protocol, so it can be bit-banged
  slowly *without* disabling interrupts — it never fights USI I²C timing the way
  WS2812's strict 800 kHz does. They latch their own state, so the MCU can sleep.
- Drive current is a *power* decision: 1–2 mA is plainly visible, and PWM dimming
  buys large savings for little perceived loss. See architecture §9.5.
- **Never run strip current through the Qwiic `VCC` line** — inject power at the
  strip.

## Status

⬜ Everything — firmware skeleton, then PCB.

Registers: see `SMARTINY_LED_REG_*` in
[`lib/smartiny-common/smartiny_regs.h`](../../lib/smartiny-common/smartiny_regs.h).
