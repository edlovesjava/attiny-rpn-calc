# modules/ — the smartiny board family

Each module is a self-contained Qwiic / STEMMA QT breakout that hides its pins,
its analog mess and its driver complexity behind the shared register model in
[`lib/smartiny-common/smartiny_regs.h`](../lib/smartiny-common/smartiny_regs.h).

Naming convention: **`smartiny-<role>`**.

## Registry

| Module | Role | Addr | `WHO_AM_I` | Brain | Status |
|---|---|---|---|---|---|
| [`smartiny-key`](smartiny-key/) | 16-key keypad → events | `0x20` | `0x01` | ATtiny85 | **in progress** — ladder locked, bench validation |
| [`smartiny-led`](smartiny-led/) | Indicator / light output | `0x21` | `0x02` | ATtiny85 | **next** — Board 0, the reference slave |
| [`smartiny-pwr`](smartiny-pwr/) | LiPo telemetry, charge state | `0x22` | `0x03` | ATtiny85 | planned |
| [`smartiny-mem`](smartiny-mem/) | NV store for user programs | `0x50` | `0x04`\* | FRAM (or ATtiny85) | planned |
| [`smartiny-calc`](smartiny-calc/) | RPN brain — bus **master** | — | `0x05` | ATtiny85 / tinyAVR-1 | planned |

\* Raw FRAM at `0x50` is a plain I2C memory with **no** common header; the
`WHO_AM_I` id applies only to a managed (MCU-fronted) variant.

`0x00` and `0xFF` are reserved as invalid ids so a stuck-low or absent bus can
never be mistaken for a real module during enumeration.

## Module layout

Each module directory follows the same shape, created as it fills:

```
smartiny-<role>/
├── README.md      role, address, pinout, register map, status
├── docs/          design notes specific to this module
├── hardware/      schematic, PCB, BOM  (one subdir per board)
├── firmware/      module firmware
│   └── core/      pure C, no hardware — unit-testable on a PC
├── tests/         host-side unit tests for core/
└── tools/         module-specific bench / design tools
```

## Build order

Product priority and learning priority deliberately differ. The keypad is the
flagship, but **`smartiny-led` gets built first** because it is the simplest
*complete* vertical slice — a full I2C slave with trivial application logic — and
becomes the reference-slave template every other module forks.
