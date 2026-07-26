#!/usr/bin/env python3
"""
key_monitor.py — watch smartiny-key events over I2C, and write its EEPROM config.

The ATtiny85 has no spare pin for a debug UART, so this is the debug channel:
drain EVENT_FIFO over the bus and pretty-print the decoded stream. Run it against
a CP2112 (Linux exposes it as /dev/i2c-N via the i2c-cp2112 driver) or any other
I2C master adapter.

    # watch the event stream
    ./key_monitor.py --bus 3

    # inspect / change configuration (persists to EEPROM)
    ./key_monitor.py --bus 3 --show
    ./key_monitor.py --bus 3 --set-mod 0 sticky 15 --hold-ms 500 --save

    # verify the decoder with no hardware attached
    ./key_monitor.py --selftest

Register and bit definitions mirror lib/smartiny-common/smartiny_regs.h.
"""

import argparse
import sys
import time

# ---- register map (see lib/smartiny-common/smartiny_regs.h) -----------------
REG_WHO_AM_I     = 0x00
REG_VERSION      = 0x01
REG_STATUS       = 0x02
REG_I2C_ADDR     = 0x04
REG_KEY_STATUS   = 0x10
REG_MODIFIERS    = 0x11
REG_EVENT_FIFO   = 0x12
REG_EVENT_COUNT  = 0x13
REG_DEBOUNCE_MS  = 0x14
REG_REPEAT_CFG   = 0x15
REG_LED_LOCAL    = 0x16
REG_HOLD_MS      = 0x17
REG_MOD_CFG      = 0x18          # 0x18..0x1B
REG_LED_MODE     = 0x1C
REG_SAVE         = 0x1F

ID_KEY           = 0x01
ADDR_KEY         = 0x20
SAVE_MAGIC       = 0x5A

EVT_NAMES  = {0: "PRESS", 1: "RELEASE", 2: "LONG", 3: "REPEAT"}
MOD_MODES  = {0: "off", 1: "momentary", 2: "sticky", 3: "lock"}
MODE_BY_NAME = {v: k for k, v in MOD_MODES.items()}


# ---- pure decoding (no hardware) -------------------------------------------
def decode_event(b):
    """Event byte -> (type_name, mods, keycode). Layout [type:2][mods:2][key:4]."""
    return EVT_NAMES[(b >> 6) & 0x03], (b >> 4) & 0x03, b & 0x0F


def pack_event(type_id, mods, key):
    return ((type_id & 0x03) << 6) | ((mods & 0x03) << 4) | (key & 0x0F)


def decode_mod_cfg(c):
    """Modifier config byte -> (mode_name, keycode). Layout [mode:2][rsvd:2][key:4]."""
    return MOD_MODES[(c >> 6) & 0x03], c & 0x0F


def pack_mod_cfg(mode_name, key):
    return ((MODE_BY_NAME[mode_name] & 0x03) << 6) | (key & 0x0F)


def format_event(b):
    name, mods, key = decode_event(b)
    row, col = key >> 2, key & 3
    tag = f" mods={mods:02b}" if mods else ""
    return f"0x{b:02X}  {name:<7} key={key:<2} (r{row},c{col}){tag}"


# ---- self test: the documented sequences double as decoder vectors ----------
def selftest():
    # Sticky SHIFT on keycode 15, then key 5. See docs/keys-and-feedback.md.
    seq = [
        (0x1F, "PRESS",   1, 15),   # shift down -> latched
        (0x5F, "RELEASE", 1, 15),   # latch survives release
        (0x15, "PRESS",   1,  5),   # key 5 carries the modifier
        (0x45, "RELEASE", 0,  5),   # mods now 0: the one-shot was consumed
        (0x07, "PRESS",   0,  7),
        (0x87, "LONG",    0,  7),
        (0xC3, "REPEAT",  0,  3),
        (0x47, "RELEASE", 0,  7),
    ]
    bad = 0
    for raw, want_name, want_mods, want_key in seq:
        name, mods, key = decode_event(raw)
        ok = (name, mods, key) == (want_name, want_mods, want_key)
        rt = pack_event(list(EVT_NAMES).index(
            [k for k, v in EVT_NAMES.items() if v == name][0]), mods, key)
        if not ok or rt != raw:
            bad += 1
            print(f"  FAIL 0x{raw:02X}: got {name},{mods},{key}")
        else:
            print(f"  ok   {format_event(raw)}")

    for raw, want_mode, want_key in [(0xCF, "lock", 15), (0x8F, "sticky", 15),
                                     (0x40, "momentary", 0), (0x00, "off", 0)]:
        mode, key = decode_mod_cfg(raw)
        if (mode, key) != (want_mode, want_key) or pack_mod_cfg(mode, key) != raw:
            bad += 1
            print(f"  FAIL mod_cfg 0x{raw:02X}: got {mode},{key}")
        else:
            print(f"  ok   mod_cfg 0x{raw:02X} -> {mode} on key {key}")

    print(f"\n{'FAILED' if bad else 'PASS'} — {bad} failure(s)")
    return 1 if bad else 0


# ---- bus access ------------------------------------------------------------
def open_bus(busno):
    try:
        from smbus2 import SMBus
    except ImportError:
        sys.exit("smbus2 not installed — `pip install smbus2`, or use --selftest")
    try:
        return SMBus(busno)
    except (OSError, PermissionError) as e:
        sys.exit(f"cannot open /dev/i2c-{busno}: {e}\n"
                 f"is the CP2112 plugged in, and are you in the i2c group?")


def identify(bus, addr):
    who = bus.read_byte_data(addr, REG_WHO_AM_I)
    if who != ID_KEY:
        sys.exit(f"device at 0x{addr:02X} reports WHO_AM_I 0x{who:02X}, "
                 f"expected 0x{ID_KEY:02X} (smartiny-key)")
    ver = bus.read_byte_data(addr, REG_VERSION)
    print(f"smartiny-key at 0x{addr:02X}, firmware v{ver >> 4}.{ver & 0x0F}")


def show_config(bus, addr):
    print(f"  I2C_ADDR    0x{bus.read_byte_data(addr, REG_I2C_ADDR):02X}")
    print(f"  DEBOUNCE_MS {bus.read_byte_data(addr, REG_DEBOUNCE_MS)}")
    print(f"  HOLD_MS     {bus.read_byte_data(addr, REG_HOLD_MS) * 10} ms")
    print(f"  REPEAT_CFG  0x{bus.read_byte_data(addr, REG_REPEAT_CFG):02X}")
    print(f"  LED_MODE    0x{bus.read_byte_data(addr, REG_LED_MODE):02X}")
    for i in range(4):
        cfg = bus.read_byte_data(addr, REG_MOD_CFG + i)
        mode, key = decode_mod_cfg(cfg)
        note = "" if mode == "off" else f" -> key {key}"
        print(f"  MOD{i}_CFG   0x{cfg:02X}  {mode}{note}")


def monitor(bus, addr, poll_hz):
    print("watching EVENT_FIFO — ctrl-C to stop\n")
    period = 1.0 / poll_hz
    t0 = time.monotonic()
    while True:
        n = bus.read_byte_data(addr, REG_EVENT_COUNT)
        for _ in range(n):
            raw = bus.read_byte_data(addr, REG_EVENT_FIFO)
            print(f"[{time.monotonic() - t0:8.3f}] {format_event(raw)}")
        st = bus.read_byte_data(addr, REG_KEY_STATUS)
        if st & 0x02:      # SMARTINY_STATUS_OVERFLOW
            print("  ** FIFO OVERFLOW — polling too slowly **")
        time.sleep(period)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bus", type=int, help="I2C bus number (/dev/i2c-N)")
    p.add_argument("--addr", type=lambda s: int(s, 0), default=ADDR_KEY)
    p.add_argument("--poll-hz", type=float, default=50.0)
    p.add_argument("--show", action="store_true", help="print configuration and exit")
    p.add_argument("--set-mod", nargs=3, metavar=("SLOT", "MODE", "KEYCODE"),
                   action="append", help="e.g. --set-mod 0 sticky 15")
    p.add_argument("--hold-ms", type=int, help="long-press threshold in ms")
    p.add_argument("--debounce-ms", type=int)
    p.add_argument("--led-mode", type=lambda s: int(s, 0))
    p.add_argument("--save", action="store_true", help="commit config to EEPROM")
    p.add_argument("--selftest", action="store_true", help="decoder check, no hardware")
    args = p.parse_args()

    if args.selftest:
        return selftest()
    if args.bus is None:
        p.error("--bus is required (or use --selftest)")

    bus = open_bus(args.bus)
    identify(bus, args.addr)

    wrote = False
    for slot, mode, key in args.set_mod or []:
        if mode not in MODE_BY_NAME:
            sys.exit(f"mode must be one of {', '.join(MODE_BY_NAME)}")
        bus.write_byte_data(args.addr, REG_MOD_CFG + int(slot),
                            pack_mod_cfg(mode, int(key)))
        print(f"  MOD{slot}_CFG <- {mode} on key {key}")
        wrote = True
    if args.hold_ms is not None:
        bus.write_byte_data(args.addr, REG_HOLD_MS, max(1, args.hold_ms // 10))
        wrote = True
    if args.debounce_ms is not None:
        bus.write_byte_data(args.addr, REG_DEBOUNCE_MS, args.debounce_ms)
        wrote = True
    if args.led_mode is not None:
        bus.write_byte_data(args.addr, REG_LED_MODE, args.led_mode)
        wrote = True
    if args.save:
        bus.write_byte_data(args.addr, REG_SAVE, SAVE_MAGIC)
        print("  config committed to EEPROM")
    elif wrote:
        print("  (live only — pass --save to persist across power cycles)")

    if args.show:
        show_config(bus, args.addr)
        return 0
    if wrote or args.save:
        return 0

    try:
        monitor(bus, args.addr, args.poll_hz)
    except KeyboardInterrupt:
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
