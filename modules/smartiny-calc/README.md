# smartiny-calc

The RPN brain — **I²C bus master**, not a slave. **`WHO_AM_I` `0x05`**

Owns the RPN stack, formats the display, polls `smartiny-key` for events, drives
the SSD1306, and orchestrates system sleep. Everything else on the bus exists to
serve it.

## Responsibilities

- RPN stack + operations; sticky-modifier *semantics* (the keypad reports which
  modifiers were latched, the host decides what they mean)
- Display formatting to the off-the-shelf **SSD1306 at `0x3C`**
- Bus enumeration: probe addresses, read `WHO_AM_I`, discover what is plugged in
- Idle/sleep policy — including issuing SSD1306 *display-off* (`0xAE`), which
  saves the most of any single logic-side measure

## The open chip decision

This is the one seat where a bare ATtiny85 is genuinely contested:

- **ATtiny85 (purist).** USI can master I²C fine, and an RPN display is
  event-driven — it updates when the stack changes, not continuously — so
  bandwidth is a non-issue. **The framebuffer is not the obstacle it looks like:**
  a full buffer is 1 KB, but `u8g2` page mode needs only 128 B and `u8x8` needs
  essentially none — and a calculator display is *text*, so `u8x8`'s 16×8
  characters are the natural fit rather than a compromise. Flash is the likelier
  ceiling: 8 KB must hold USI master, the display driver, the RPN engine and
  eventually a program VM.
- **tinyAVR-1 / megaAVR-0.** Hardware TWI, RAM for a framebuffer, a real USART
  (which is what lets USB-over-UART work while the calculator keeps running), a
  spare pin for the shared `INT` line, and UPDI programming that costs no I/O.

The USB story pushes hardest toward the upgrade — see architecture §11 decision 9.

## Status

⬜ Planned — Board 2, after the keypad.
