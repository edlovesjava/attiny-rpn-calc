# megaAVR-0-programmer

Notes and setup guide for bringing up a **standalone ATmega4809** (40-pin DIP) on a
breadboard and programming it over **UPDI** using an **Arduino Nano** as the programmer
(via [jtag2updi](https://github.com/ElTangas/jtag2updi)), with a **CP2102** USB-to-TTL
adapter for serial debugging.

The whole toolchain, in one picture:

```
                 USB                          UPDI (1-wire)
  PC ────────────────► Arduino Nano ───────────────────────► ATmega4809
  (Arduino IDE)        (jtag2updi)     D6 ─4.7kΩ─ pin 30       (target)

  PC ────────────────► CP2102 ◄──── PA0/PA1 (USART0) ───────► ATmega4809
  (Serial Monitor)     (USB-TTL)                               (target)
```

- **Nano** = the programmer. Flashes the 4809 over UPDI.
- **CP2102** = the serial monitor. Talks to the 4809's UART.
- Both can stay plugged in at once — flash with one, watch output on the other, no rewiring.

---

## Bill of materials

| Item | Notes |
|---|---|
| ATmega4809 | 40-pin PDIP |
| Arduino Nano | ATmega328P — becomes the UPDI programmer |
| CP2102 USB-to-TTL adapter | for serial debugging |
| 4.7 kΩ resistor | in the UPDI line (D6 → pin 30) |
| 0.1 µF ceramic capacitors ×3 | one per VDD/GND pair |
| 1–10 µF capacitor ×1 | bulk decoupling across the rail |
| LED + 330 Ω resistor | optional, for the blink test |
| Breadboard + jumper wires | |

No external crystal is required — the 4809 runs off its internal oscillator.

---

## ATmega4809 40-pin PDIP essentials

Only the pins that matter for bring-up (confirm against the
[Microchip datasheet](https://www.microchip.com/en-us/product/atmega4809) /
[MegaCoreX pinout](https://github.com/MCUdude/MegaCoreX)):

| Function | Pin(s) |
|---|---|
| **UPDI** (programming) | **30** |
| VDD / GND pairs | **(5, 6)**, **(17, 18)**, **(31, 32)** |
| `Serial` / USART0 TX | **PA0** (Arduino pin 0) |
| `Serial` / USART0 RX | **PA1** (Arduino pin 1) |

> **Connect *every* VDD and *every* GND pin.** A floating supply pin on a megaAVR-0
> causes brown-outs, resets, and flaky UPDI. Put one 0.1 µF cap across *each* pair,
> placed as close to the pins as possible, plus one bulk cap across the rail.

---

## Step 1 — Build the programmer (Nano + jtag2updi)

Do this **before** wiring anything to the 4809. USB goes straight from the PC to the Nano.

1. Clone the firmware:
   ```
   git clone https://github.com/ElTangas/jtag2updi.git
   ```
2. **Rename the `source/` folder to `jtag2updi/`.** The Arduino IDE requires the sketch
   folder name to match the `.ino` file name (`jtag2updi.ino`). If it doesn't match, the
   IDE compiles only the (empty) `.ino` and leaves the `.cpp` files behind.
3. Open `jtag2updi/jtag2updi.ino` in the Arduino IDE.
4. **Confirm the tab bar shows many tabs** — `jtag2updi`, `JTAG2.cpp`, `sys.cpp`,
   `updi_io.cpp`, etc. If you only see one tab, the `.cpp` files aren't in the folder and
   it will not compile.
5. Board settings:
   - **Tools → Board → Arduino AVR Boards → Arduino Nano**
   - **Tools → Processor → ATmega328P** (or *"ATmega328P (Old Bootloader)"* for clones)
   - Select the Nano's COM port
6. **Upload.**

This is a one-time flash. The Nano stays a UPDI programmer until you overwrite it.

### Gotcha: `undefined reference to 'setup' / 'loop'`

The whole program (including `main()`, `setup()`, `loop()`) lives in `jtag2updi.cpp`;
the `.ino` is intentionally empty. This error means the `.cpp` files aren't being
compiled — caused by:

- **Wrong folder name** (see step 2), or
- Clicking **"OK"** on the IDE's *"needs to be in a sketch folder, create one?"* popup,
  which copies only the empty `.ino` into your sketchbook and abandons the `.cpp` files, or
- **Wrong board selected** (compiling for MegaCoreX/ATmega4809 instead of the Nano).

Fix: get all files into one folder named `jtag2updi`, open the `.ino` from there, confirm
the extra tabs appear, and make sure the board is Arduino Nano.

---

## Step 2 — Wire the Nano to the 4809 (UPDI)

Unplug USB while wiring.

```
Nano D6 ──/\/\/\── 4809 pin 30 (UPDI)
           4.7 kΩ
Nano GND ───────── 4809 GND        (shared ground — REQUIRED)
```

- Only these two connections between the boards.
- The 4809 keeps its own power and decoupling caps.
- **Do not** back-power the 4809 from the Nano.

---

## Step 3 — Configure the Arduino IDE for the 4809

Install [MegaCoreX](https://github.com/MCUdude/MegaCoreX) (add its Boards Manager URL,
then install), then:

- **Tools → Board → MegaCoreX → ATmega4809**
- **Tools → Chip → ATmega4809**
- **Tools → Clock → Internal 16 MHz** (safe default)
- **Tools → Programmer → JTAG2UPDI**
- **Tools → Port →** the Nano's COM port

---

## Step 4 — Test the link: Burn Bootloader

**Tools → Burn Bootloader.** This reads the device signature, sets the fuses, and confirms
UPDI communication.

- **Success** → `Done burning bootloader`. UPDI is working.
- The warning `no flash data found ... empty.hex` / `0 bytes of flash verified` is
  **normal** — MegaCoreX writes an empty bootloader placeholder because you're programming
  over UPDI (no Optiboot). It is not an error.

If it **fails on device signature / timeout**, check in order:
1. Missing shared GND between Nano and 4809
2. 4.7 kΩ not in the D6 line, or wrong UPDI pin (must be pin 30)
3. A VDD or GND pin left floating on the 4809
4. Wrong COM port

You only need to Burn Bootloader again when changing the clock or other fuse settings.

---

## Step 5 — Flash a sketch (blink)

Use **Sketch → Upload Using Programmer** (`Ctrl+Shift+U`) — **not** plain Upload
(that would try a serial bootloader you don't have).

```cpp
// PIN_PA0 is Arduino pin 0 on MegaCoreX. Any GPIO works.
#define LED PIN_PA0

void setup() {
  pinMode(LED, OUTPUT);
}

void loop() {
  digitalWrite(LED, HIGH);
  delay(500);
  digitalWrite(LED, LOW);
  delay(500);
}
```

Wire an LED to see it:

```
4809 PA0 ──[330 Ω]──►|── GND
                      LED   (long leg toward the resistor/pin)
```

---

## Step 6 — Serial debugging with the CP2102

The Nano running jtag2updi is **not** a serial passthrough, so use the CP2102 for serial
and leave the Nano as the programmer. The default `Serial` is **USART0** on **PA0 (TX)**
and **PA1 (RX)**.

Wire it (TX↔RX crosses over):

```
CP2102 RX  ◄──────────  4809 PA0 (TX)
CP2102 TX  ──────────►  4809 PA1 (RX)
CP2102 GND ──────────── 4809 GND  (shared)
```

Match the CP2102's VCC/logic level to the 4809's supply. Test sketch:

```cpp
void setup() { Serial.begin(9600); }
void loop() {
  Serial.println("hello from 4809");
  delay(1000);
}
```

Flash it (Upload Using Programmer, through the Nano), then open **Serial Monitor on the
CP2102's COM port** (a *different* port than the Nano — it enumerates as a Silicon Labs
CP210x device), baud **9600**.

Serial troubleshooting:
- **Garbled text** → baud mismatch. Confirm 9600 on both sides.
- **Nothing at all** → TX/RX not crossed (swap the two signal wires), or missing shared GND.
- **Wrong port** → make sure you selected the CP2102, not the Nano.

---

## Working configuration (quick reference)

| Setting | Value |
|---|---|
| Programmer firmware | jtag2updi on Arduino Nano (ATmega328P) |
| UPDI wiring | Nano D6 ─4.7 kΩ─ 4809 pin 30, shared GND |
| Target board | MegaCoreX → ATmega4809 |
| Programmer | JTAG2UPDI |
| Flash command | Sketch → Upload Using Programmer |
| Clock | Internal 16 MHz |
| Serial | CP2102 ↔ PA0 (TX) / PA1 (RX), USART0 @ 9600 |

---

## References

- [MegaCoreX](https://github.com/MCUdude/MegaCoreX) — Arduino core for the megaAVR-0 series
- [jtag2updi](https://github.com/ElTangas/jtag2updi) — UPDI programmer firmware for classic AVR Arduinos
- [ATmega4809 product page / datasheet](https://www.microchip.com/en-us/product/atmega4809)
