# smartiny-pwr

Power input, regulation and telemetry. **Addr `0x22` · `WHO_AM_I` `0x03`**

Feeds the bus from pluggable sources (LiPo / barrel / USB) and — in its smart
form — reports what the battery is doing so the host can draw a low-battery icon
or drop into low-power mode.

## Two flavours

- **Dumb (start here):** input mux + regulation + protection (reverse-polarity,
  polyfuse). No I²C at all. Perfectly fine to begin with.
- **Smart:** an ATtiny85 I²C slave sensing the cell through a **÷2 divider** (a
  LiPo's 4.2 V exceeds the 3.3 V ADC reference), WDT-waking every few seconds to
  sample and sleeping in between.

## Target topology

3.3 V bus from a single LiPo: **TP4056** charge from USB (with DW01 protection),
**MCP1700-3.3** LDO (µA quiescent) or a **TPS63xxx** buck-boost to use the full
3.0–4.2 V range. For charge-while-running prefer a load-sharing power-path
charger (MCP73871 / BQ24074) over a bare TP4056.

## Rules that must not be broken

- **Two rails.** Logic rail rides the Qwiic `VCC`; high-current LED loads get a
  separate injected rail. Common ground.
- **Pull-ups live on the master only** — never on this or any other module.
- **BOD costs ~20 µA continuously**, which dominates a µA sleep budget. Either
  accept that floor or disable BOD in deep sleep and lean on the LiPo protection
  cutoff plus a startup voltage check.

## Status

⬜ Planned. See architecture §9 for the full power design.

Registers: see `SMARTINY_PWR_REG_*` in
[`lib/smartiny-common/smartiny_regs.h`](../../lib/smartiny-common/smartiny_regs.h).
