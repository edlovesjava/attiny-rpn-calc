# lib/ — shared libraries

Code shared by every module. The split follows the layering in
[`docs/architecture.md`](../docs/architecture.md) §4: keep the interesting logic
MCU-independent so it can be unit-tested on a PC, and confine chip specifics to a
thin HAL.

| Library | What it is | Depends on |
|---|---|---|
| **`smartiny-common/`** | The register model: common header, `WHO_AM_I` ids, default addresses, event encoding. Pure definitions, no code. | nothing |
| **`smartiny-slave/`** | The I2C slave engine every module shares — register read/write dispatch, TinyWireS callbacks, EEPROM-persisted address. | common, hal |
| **`smartiny-hal/`** | Thin per-chip abstraction: ADC read, GPIO, timers, sleep. One implementation per target (`attiny85`, `tinyavr`, `host`). | nothing |

The `host` HAL implementation is what lets `*-core` logic (debounce, event FIFO,
modifier state machine) compile and run under a test harness on a laptop.

`smartiny-common/smartiny_regs.h` is the single source of truth for the bus
contract, shared by module firmware, the RPN host, and the PC-side test tools —
so it stays dependency-free (`stdint.h` only).
