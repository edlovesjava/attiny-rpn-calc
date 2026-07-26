# smartiny-key — worked layouts

Two real keymaps for the RPN calculator, showing how the same 16-switch pad
serves both. The keypad stores *behaviour* (which key latches, which defers to a
hold); the host stores *meaning* (what each keycode does). Neither knows the
other's business.

## The two independent dimensions

The insight that makes both layouts work from one mechanism:

| | triggers on **press** | triggers on **long hold** |
|---|---|---|
| **modifier slot** | dedicated FUNC key — short press latches | hex mode — short press is the digit, hold latches |
| **no modifier slot** | ordinary key | dual-function key; host decides what `LONG` means |

"How a key is triggered" is the `TAPHOLD` mask (`0x1D`/`0x1E`, one bit per
keycode). "What the latch does" is the modifier mode (`OFF` / `MOMENTARY` /
`STICKY` / `LOCK`). They compose freely.

## Placement rule: put committing keys on collision-proof slots

From [`ladder.md`](ladder.md) § Multi-press behaviour, a collision never decodes
above the lowest key pressed. Working through all 120 pairs, four keycodes are
**never** produced by any two-key collision:

> **Collision-proof: keycodes 10, 11, 14, 15.**

Put the keys you least want fired by accident — `ENTER`, `FUNC`, `CLEAR` — on
those. Digits belong on low keycodes, where a spurious press is merely a wrong
digit you can backspace. (Keycode 0 is the absorbing element: any collision
involving it reads as exactly 0, so it is the *worst* slot for anything
consequential.)

## Layout A — decimal RPN (16 keys, exact fit)

```
        col0    col1    col2    col3
row0     7       8       9       /          keycodes  0  1  2  3
row1     4       5       6       *                    4  5  6  7
row2     1       2       3       -                    8  9 10 11
row3     0      FUNC   ENTER     +                   12 13 14 15
```

`ENTER` = 14 and `+` = 15 are both collision-proof. `FUNC` at 13 is not — if
that bothers you, swap `FUNC` and `+`, since a spurious `+` is easier to notice
and undo than a silently armed modifier.

Config: **no tap-hold at all.** `FUNC` is a dedicated key, so a short press
latches it — no holding required, exactly as it should feel.

```console
$ ./key_monitor.py --bus 3 --set-mod 0 sticky 13 --save
```

### Session trace: `10 ENTER 5 + 2 FUNC *` → 225

| You press | Events | OLED |
|---|---|---|
| `1` | `0x08` `0x48` | `1` |
| `0` | `0x0C` `0x4C` | `10` |
| `ENTER` | `0x0E` `0x4E` | `10` pushed |
| `5` | `0x05` `0x45` | `5` |
| `+` | `0x0F` `0x4F` | `15` |
| `2` | `0x09` `0x49` | `2` |
| `FUNC` | `0x1D` `0x5D` | — · LED **fast blink** |
| `*` | `0x17` `0x47` | `225` |

The last two rows are the whole modifier design in miniature. `FUNC`'s own press
already reports `mods=1` (`0x1D`), the latch survives release (`0x5D`), and then
`*` arrives as `0x17` — **`mods=1`, keycode 7** — so the host reads it as `^`
rather than `×` and computes 15². Its release comes back as `0x47` with `mods=0`,
which is the observable marker that the one-shot was spent and the LED has gone
dark.

Note what the keypad never knew: that `*` means multiply, that `FUNC`+`*` means
power, or that a calculator was involved at all.

## Layout B — hex entry (all 16 keys are digits)

```
        col0    col1    col2    col3
row0     0       1       2       3
row1     4       5       6       7
row2     8       9       A       B
row3     C       D       E       F         keycode == hex value
```

Every key is a digit, so there is no room for dedicated `ENTER` or `FUNC` — they
move onto **long holds** of two keys, which is exactly what the `TAPHOLD` mask is
for. Both `E` (14) and `F` (15) are collision-proof slots, which is why they are
the right pair to overload.

| Key | Short press | Long hold |
|---|---|---|
| `E` (14) | hex digit `E` | latch `FUNC` (sticky) |
| `F` (15) | hex digit `F` | `ENTER` |

```console
$ ./key_monitor.py --bus 3 \
      --set-mod 0 sticky 14 --taphold 14 --taphold 15 --hold-ms 400 --save
```

- `E` is a **modifier slot with its tap-hold bit set** → the latch triggers on
  `LONG` instead of `PRESS`.
- `F` is **tap-hold with no modifier slot** → the module just reports `LONG`, and
  the host maps that to `ENTER`. No keypad feature needed; this is the
  "module reports facts, host assigns meaning" split doing real work.

### Session trace: enter `A`, then `F`-hold as ENTER

| You do | Events | Meaning |
|---|---|---|
| tap `A` | `0x0A` `0x4A` | digit `A` — not tap-hold, so `PRESS` fires immediately |
| tap `F` | `0x0F` `0x4F` | digit `F` — deferred to release, then emitted |
| **hold** `F` | `0x8F` `0x4F` | `LONG` → host does `ENTER`; **no `PRESS`**, so the digit is suppressed |
| **hold** `E` | `0x9E` `0x5E` | `LONG` with `mods=1` → `FUNC` latched, LED fast blink |

The `LONG`-instead-of-`PRESS` distinction is what lets one key be both a digit
and a command with no ambiguity for the host.

## Cost of tap-hold, and where not to use it

- **A tap-hold key fires on release, not press.** It has to — at press time the
  module cannot know what is coming. For a quick tap that is only your own press
  duration (~60–100 ms), but it *is* a different feel, so only mask the keys that
  need it. Layout A masks none.
- **A tap-hold key cannot auto-repeat**, since holding is taken. Never mask a key
  you want to hold down.
- Keep `HOLD_MS` around 300–400 ms for a key you also type normally; 500 ms+ for
  a pure modifier.

## Power-on

`smartiny-key` blinks its LED **three times** at boot: the ADC decode, debounce
and I²C stack came up. It happens before any bus traffic, so it is also the first
thing to look at when a module seems dead. The host separately shows *ready* on
the OLED once enumeration succeeds — the LED says "the keypad is alive", the OLED
says "the system is ready", and during bring-up the difference tells you which
half is broken.
