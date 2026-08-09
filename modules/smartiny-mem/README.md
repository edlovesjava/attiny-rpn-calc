# smartiny-mem

Non-volatile store for user RPN programs and state. **Addr `0x50` · `WHO_AM_I` `0x04`\***

This module is what makes a *programmable* calculator possible at all: an
ATtiny85 has 512 B of RAM and 8 KB of flash, so user programs and constants have
to live off-chip.

## Two flavours

- **Raw memory (start here):** an I²C **FRAM** (FM24CL64) or EEPROM (24LC256/512)
  is already a native I²C slave — no MCU, no firmware. The host reads and writes
  it directly at `0x50–0x57`, clear of `0x20`/`0x21`/`0x3C`.
  **\*A raw part has no common register header**, so it does not answer
  `WHO_AM_I`; the id applies only to a managed variant.
- **Managed:** an ATtiny85 in front of the memory exposing program slots
  (`list`/`load`/`store`/`erase`) instead of raw bytes.

**FRAM is the better fit for a calculator:** instant writes, no page-write delay,
effectively unlimited endurance — good for continuously saving stack and program
state.

## Open question

"Programmable" splits two ways and this module only serves the first:
**user-programmable** (write/store/run RPN programs — needs this store) versus
**firmware-reprogrammable** (update the module's own firmware — a bootloader/UPDI
concern). See architecture §11 decision 8.

## Status

⬜ Planned — compelling next-up work once the keypad and host exist.
