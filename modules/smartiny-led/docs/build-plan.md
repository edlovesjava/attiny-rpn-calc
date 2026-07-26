# smartiny-led — PoC build plan

Board 0. The goal is **not** the LEDs — it is to prove the bus plumbing every
later module inherits: common register header, address persistence, Qwiic
wiring, pull-up and power discipline. Get this right on easy mode and
`smartiny-key` becomes a fork rather than a fresh start.

Each stage below ends in something observable. Do not move on until it is.

## Stage 0 — logic on the laptop (no hardware) ✅

```console
$ cd modules/smartiny-led/tests && make
42 checks, 0 failures
```

Covers the register engine (common header, read-only identity, pointer
auto-increment), address validation, and soft-PWM duty. All of it runs before
any part is bought — this is the payoff of the `core` / `platform` split.

## Stage 1 — blink, no bus

Breadboard the ATtiny85 with its 3 LEDs and decoupling. Flash the sketch and
confirm the power-on sweep (LED0 → LED1 → LED2).

Prerequisite: a working ISP path — Nano-as-ISP or a USBasp. A soldered
programming jig pays for itself here, since ISP shares pins with the bus and
chips come out to be reflashed constantly; see
[`docs/bench-setup.md`](../../../docs/bench-setup.md) § Bench builds.

**Proves:** the chip is alive, the fuses/clock are right, the LED pins and
polarity are correct. If the sweep doesn't run, nothing after this matters.

- Set the '85 to **8 MHz internal RC** — no crystal, safe at 3.3 V.
- Program **before** wiring the bus (ISP shares PB0/PB1/PB2 — see
  `hardware/README.md`).

## Stage 2 — talk to it from a PC

Wire SDA/SCL/GND/VCC to the **CP2112** dongle. Keep the whole bench at **one
voltage** — 5 V dongle → power the '85 at 5 V too. Never put a 5 V bus on a
3.3 V-powered AVR: pins are only VCC+0.5 tolerant.

```console
$ i2cdetect -y <bus>          # expect a device at 0x21
$ i2cget -y <bus> 0x21 0x00   # WHO_AM_I  -> 0x02
$ i2cget -y <bus> 0x21 0x01   # VERSION   -> 0x01  (v0.1)
$ i2cget -y <bus> 0x21 0x10   # LED_COUNT -> 0x03
$ i2cset -y <bus> 0x21 0x11 0x05   # STATE  -> LED0 + LED2 on
$ i2cset -y <bus> 0x21 0x12 0x20   # BRIGHTNESS -> dim
```

**Proves:** USI slave works, the register model works end-to-end, and the module
is discoverable by `WHO_AM_I` exactly as bus enumeration will do it.

- Pull-ups: the CP2112's internal ones usually suffice on a short bench bus. If
  `i2cdetect` finds nothing, add **4.7 kΩ** to VCC on SDA and SCL — and remember
  these belong to the *master*, never to this board.
- Run the dongle in **I²C mode, not strict SMBus**: SMBus's ~35 ms clock-stretch
  timeout can trip a slow slave.
- Start at **100 kHz**.

## Stage 3 — address persistence

```console
$ i2cset -y <bus> 0x21 0x04 0x31   # move it to 0x31
$ i2cdetect -y <bus>               # now answers at 0x31
# power-cycle
$ i2cdetect -y <bus>               # STILL 0x31 — it came from EEPROM
```

Also confirm bad addresses are refused (`0x00`, `0x78`, `0x7F` must leave it
where it was). This is the register that can strand a module, so verify the
guard on real hardware, not just in the unit tests.

**Proves:** the EEPROM path works and two identical modules could coexist —
which is what makes the family scalable.

## Stage 4 — two devices on one bus

Add any second I²C device (an SSD1306 at `0x3C` is ideal — you will need it
anyway). Confirm `i2cdetect` shows both and that talking to one does not disturb
the other.

**Proves:** it is genuinely a *bus*, not a point-to-point link — the first real
test of the multi-drop architecture.

## Stage 5 — a real master

Replace the PC dongle with an ATtiny85 running as I²C **master**, driving the
LED module. This is the first end-to-end smartiny-only system, and it is the
skeleton `smartiny-calc` grows from.

**Proves:** a '85 can master this bus — the assumption the whole platform rests on.

## Stage 6 — perfboard, then PCB

Move to perfboard with real Qwiic connectors, then lay out the PCB. By this
point the firmware is unchanged since Stage 2, which is the whole idea.

---

## Done when

- [ ] Host tests pass
- [ ] Power-on LED sweep runs
- [ ] `WHO_AM_I` reads `0x02` over I²C
- [ ] `STATE` and `BRIGHTNESS` visibly control the LEDs
- [ ] Address change survives a power cycle; invalid addresses rejected
- [ ] Coexists with a second device on the bus
- [ ] Driven by a '85 master

## Then

Fork this into `smartiny-key`: same common header, same address persistence,
same main-loop discipline — swap `led_core` for the ADC ladder decode, debounce,
modifier state machine and event FIFO.

## Notes for later (deliberately not in v1)

- **Sleep.** v1 stays awake; the USI start-condition interrupt is what will wake
  it (architecture §9.6).
- **The `INT` line.** The LED module has nothing to report, so it does not need
  one. `smartiny-key` does.
- **APA102 / RGB.** v2. Keep v1 dumb.
