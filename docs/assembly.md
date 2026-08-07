# Physical assembly — how the boards actually connect

The bus is one thing electrically and another thing physically. §3 covers the
electrical rules; this covers what you hold in your hand.

> **Electrically it is always a multi-drop bus** — every device taps the same
> SDA/SCL in parallel. Whether those taps are cables or a stack of headers changes
> nothing about the signals. That is why the same modules serve both.

## Two physical forms, one bus

| Form | When | Carries |
|---|---|---|
| **Qwiic cables** (JST-SH 4-pin) | bench, bring-up, off-the-shelf sensors | SDA/SCL/VCC/GND |
| **The spine** (1×5 through-header) | assembled product | SDA/SCL/VCC/GND **+ `INT`** |

Same signals, two footprints — exactly the `SMARTINY-6` pattern again. Fit both;
they cost pennies and serve different days.

### Why the assembled form needs the spine

Qwiic is **strictly 4-pin**, and the shared open-drain `INT` line is a fifth wire.
Without it the master cannot be woken by a keypress and must poll — which forfeits
the whole deep-sleep story in §9.6.

So on cables, `INT` is a flying lead you add when you want it. On the spine it is
pin 5 and always present. **That, not tidiness, is the real argument for the
stack.**

## Bench form — flat and cabled

What you will actually build first. Nothing is enclosed; everything is reachable.

```
   ┌────────┐  SMARTINY-6   ┌──────────────┐
   │ dock   │──────────────▶│ module under │   ← program mode: isolated
   │ (Nano) │               │    test      │
   └───┬────┘               └──────────────┘
       │ Qwiic
       ▼
   ┌────────┐   ┌────────┐   ┌────────┐   ┌────────┐
   │ key    │───│ led    │───│ OLED   │───│ pwr    │   ← bus mode: chained
   │ 0x20   │   │ 0x24   │   │ 0x3C   │   │ 0x28   │
   └────────┘   └────────┘   └────────┘   └────────┘
        └───────── INT: flying lead back to the dock ─────────┘
```

Each module carries **two Qwiic connectors** wired in parallel so the bus passes
through — that is what makes the chain possible without a hub. Remember the chain
is a convenience of *cabling*, not a signal path: pull a module from the middle
and everything downstream still works, because they were never in series.

## Assembled form — the calculator

The keypad is not a peripheral here; **it is the front panel.** That decides the
stack order.

```
              ┌───────────────────────┐
              │   OLED  ·  SSD1306    │   0x3C   top of the panel
              ├───────────────────────┤
              │                       │
              │   smartiny-key        │   0x20   the front panel itself
              │   16 keys + status    │          (16 keys, one LED)
              │                       │
              └───────────┬───────────┘
                          │  spine: GND · VCC · SDA · SCL · INT
              ┌───────────┴───────────┐
              │   smartiny-calc       │   master   brain, behind the panel
              ├───────────────────────┤
              │   smartiny-pwr        │   0x28     LiPo + charge + rails
              └───────────────────────┘
                    ▲            ▲
                 USB port    LiPo cell
```

Points worth noting in that arrangement:

- **The keypad's footprint sets the product's footprint.** Everything else hides
  behind it, so board outlines should match the keypad rather than the other way
  round.
- **`smartiny-pwr` sits at the bottom**, nearest the cell and the USB jack, so the
  high-current path never crosses the logic boards. This is the two-rails rule
  (§9.3) expressed as physical layout.
- **The OLED stays a separate board** because it is off-the-shelf — it plugs in
  rather than being designed in, which keeps it swappable.
- `smartiny-led` is absent: in the assembled calculator the OLED carries status
  and the keypad's own LED carries modifier state. The LED module is a bench
  fixture and a learning board, not a resident (see its README).

## The spine

A 1×5, 0.1″ through-header — male below, female above — so boards stack and pass
the bus upward.

| Pin | Signal | |
|---|---|---|
| 1 | GND | Qwiic order… |
| 2 | VCC | …so a 4-pin Qwiic cable maps straight onto pins 1–4 |
| 3 | SDA | |
| 4 | SCL | |
| 5 | **`INT`** | open-drain, wired-OR, the pin Qwiic cannot carry |

Deliberately the same first four signals in the same order as Qwiic and as
`SMARTINY-6` pins 1–4. **One signal order, three connectors** — a cable, a stack,
and a programming header — so nothing needs a crossover adapter.

> **Pull-ups live on `smartiny-calc`**, the master, wherever it sits in the stack
> (§3 rule 2). Not on the spine, not on each board.

## Which to build when

1. **Now — Qwiic cables only.** Flat on the bench, nothing enclosed, `INT` as a
   flying lead if you want wake testing. Fastest to change.
2. **After the modules work — add the spine footprint** to each board. It costs a
   header and lets you stack without redesigning anything.
3. **Enclosure last.** The keypad outline is the constraint; do not cut plastic
   until the panel board is final.
