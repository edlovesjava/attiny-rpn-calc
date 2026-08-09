# smartiny-hal — deferred until it earns its keep

Intentionally empty for now.

For `smartiny-led`, the Arduino layer **is** the HAL: the entire platform
dependency is three `PORTB` bit operations and `EEPROM.read/update`, which live
directly in the sketch. Wrapping that in an abstraction would be ceremony, not
engineering.

This library materialises when **`smartiny-key`** needs it, because that module
has genuinely chip-specific behaviour worth isolating:

- ADC read with the reference forced to VCC (ratiometric decode depends on it)
- PCINT configuration for wake-on-keypress via the `SENSE` line
- sleep-mode entry/exit and the wake-source setup
- EEPROM access

At that point expect `smartiny_hal.h` plus `_attiny85.c`, `_tinyavr.c` and a
`_host.c` stub — the last being what lets keypad `core` logic (debounce, event
FIFO, modifier state machine) run under the PC test harness.
