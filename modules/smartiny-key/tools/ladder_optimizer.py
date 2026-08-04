#!/usr/bin/env python3
"""
Keypad resistor-ladder optimizer.

Decodes a passive 4x4 matrix (16 keys, 8 leads) on ONE ADC pin using a base-4
positional ladder:

    VCC --[Rr_i]-- Row_i        SENSE --[Rc_j]-- Col_j
    (i=0..3)                    (j=0..3)
                               SENSE --[Rload]-- GND
                               SENSE --------> ADC

Press key (row i, col j) closes the only path:
    VCC -> Rr_i -> [row<->col short] -> Rc_j -> SENSE -> Rload -> GND
so the series resistance is R_total = Rr_i + Rc_j and

    V_sense / VCC = Rload / (R_total + Rload)          (ratiometric -> voltage-independent)
    keycode       = 4*i + j                            (0..15)

Idle (no key) -> SENSE pulled to 0 by Rload -> ADC 0, far from any press.

Because the divider is NONLINEAR and the resistors are constrained to an ADDITIVE
grid of E24 values, there is no closed form: we search for the 8 resistor values
(+ Rload) that MAXIMIZE THE MINIMUM ADC GAP between adjacent keycodes, subject to
two hard constraints:

  1. Monotonic decode: R_total strictly increases with keycode
     -> requires each row step > total column span (the base-4 condition).
  2. Wake floor (see architecture.md 9.6): the dimmest press (keycode 15, the
     LOWEST voltage) must still exceed the PCINT digital-HIGH threshold
     (VIH ~ 0.6*VCC) so ANY keypress wakes the sleeping MCU on the SENSE line.

Run:  python3 tools/ladder_optimizer.py
"""

import itertools
import random

# ---- knobs -------------------------------------------------------------------
ADC_BITS       = 10
ADC_MAX        = (1 << ADC_BITS) - 1          # 1023
MIN_RESISTOR   = 1000.0                        # keep >= 1k so ~30-50ohm switch contact R is <5%
TRIALS         = 3_000_000
SEED           = 12345                          # fixed: reproducible (no Date/random-entropy dependence)

# Design frontier: each row is a wake strategy = a floor on the DIMMEST press
# (keycode 15). Lower floor -> more ADC range for decode -> bigger gaps, but the
# wake must then come from something other than the PB3 digital threshold.
SCENARIOS = [
    ("decoupled  (comparator/master-poll wake)", 0.05),
    ("PCINT wake (VIH spec floor 0.60)",         0.60),
    ("PCINT wake + margin (0.65)",               0.65),
]

E24 = [1.0,1.1,1.2,1.3,1.5,1.6,1.8,2.0,2.2,2.4,2.7,3.0,3.3,3.6,3.9,4.3,4.7,5.1,5.6,6.2,6.8,7.5,8.2,9.1]
E12 = [1.0,1.2,1.5,1.8,2.2,2.7,3.3,3.9,4.7,5.6,6.8,8.2]

# Which preferred-value series to draw from. E12 is what a hobby parts drawer
# actually holds; restricting to it costs nothing here — see SERIES_NOTE below.
SERIES = {"e12": E12, "e24": E24}

def decade(series, lo, hi):
    vals = []
    for dec in (1e2, 1e3, 1e4):
        for b in series:
            v = b * dec
            if lo <= v <= hi:
                vals.append(round(v, 1))
    return sorted(set(vals))

def make_pools(series):
    return (decade(series, 1_000, 4_700),      # column resistors (the fine digit)
            decade(series, 4_700, 47_000),     # row resistors (the coarse digit)
            decade(series, 10_000, 100_000))   # load / pull-down

COL_POOL, ROW_POOL, RLOAD_POOL = make_pools(E24)

# ---- model -------------------------------------------------------------------
def fracs(rr, rc, rload):
    """Return the 16 SENSE/VCC fractions indexed by keycode = 4*i + j."""
    out = [0.0] * 16
    for i in range(4):
        for j in range(4):
            rtot = rr[i] + rc[j]
            out[4 * i + j] = rload / (rtot + rload)
    return out

def evaluate(rr, rc, rload, wake_frac_min):
    """Score a candidate. Higher is better; return None if a hard constraint fails."""
    # constraint 1: monotonic decode (voltage strictly decreasing as keycode rises)
    f = fracs(rr, rc, rload)
    for k in range(15):
        if not (f[k] > f[k + 1]):
            return None
    # constraint 2: wake floor on the dimmest press (keycode 15)
    if f[15] < wake_frac_min:
        return None
    codes = [round(ADC_MAX * x) for x in f]
    gaps = [codes[k] - codes[k + 1] for k in range(15)]
    return min(gaps), codes, gaps, f

# ---- search ------------------------------------------------------------------
def search(wake_frac_min):
    global COL_POOL, ROW_POOL, RLOAD_POOL
    rng = random.Random(SEED)
    best = None
    for _ in range(TRIALS):
        c1, c2, c3 = sorted(rng.sample(COL_POOL, 3))
        col_span = c3                                   # Rc[0]=0, so span = c3
        # rows: enforce each step > col span so keycode order == resistance order
        r1 = rng.choice(ROW_POOL)
        if r1 <= col_span:                              # step 0->1 must exceed span
            continue
        r2 = rng.choice(ROW_POOL)
        r3 = rng.choice(ROW_POOL)
        if not (r2 - r1 > col_span and r3 - r2 > col_span and r1 < r2 < r3):
            continue
        rload = rng.choice(RLOAD_POOL)
        rr = [0.0, r1, r2, r3]
        rc = [0.0, c1, c2, c3]
        res = evaluate(rr, rc, rload, wake_frac_min)
        if res is None:
            continue
        score = res[0]
        if best is None or score > best[0]:
            best = (score, rr, rc, rload, res[1], res[2], res[3])
    return best

def thevenin_kohm(rtot, rload):
    return (rtot * rload) / (rtot + rload) / 1000.0

def report(name, wake_frac_min, best):
    if best is None:
        print(f"\n### {name}: no feasible solution.")
        return
    score, rr, rc, rload, codes, gaps, f = best
    print("=" * 70)
    print(f"{name}")
    print("=" * 70)
    print(f"Row resistors  Rr = {[int(v) for v in rr]}  ohms")
    print(f"Col resistors  Rc = {[int(v) for v in rc]}  ohms")
    print(f"Load resistor  Rload = {int(rload)} ohms")
    print(f"Min adjacent ADC gap = {score} counts")
    print(f"keycode-15 frac = {f[15]:.3f}  (floor {wake_frac_min})  "
          f"margin {f[15]-wake_frac_min:+.3f} = {round(ADC_MAX*(f[15]-wake_frac_min))} counts")
    zmax = max(thevenin_kohm(rr[k // 4] + rc[k % 4], rload) for k in range(16))
    print(f"Max source impedance = {zmax:.1f} kohm (worst at keycode 15)")
    print("-" * 70)
    print(f"{'key':>3} {'row':>3} {'col':>3} {'Rtot(ohm)':>10} {'frac':>7} {'ADC':>5} {'gap':>5}")
    for k in range(16):
        i, j = k // 4, k % 4
        rtot = rr[i] + rc[j]
        gap = gaps[k] if k < 15 else 0
        print(f"{k:>3} {i:>3} {j:>3} {int(rtot):>10} {f[k]:>7.3f} {codes[k]:>5} {gap:>5}")
    thr = []
    for k in range(16):
        hi = codes[k]
        lo = codes[k + 1] if k < 15 else round(codes[15] * 0.5)   # below k15, above idle~0
        thr.append((hi + lo) // 2)
    print("KEY_THRESH[16] = {" + ", ".join(str(t) for t in thr) + "}  (ADC>=thresh -> keycode)")


def main():
    global COL_POOL, ROW_POOL, RLOAD_POOL
    import argparse
    ap = argparse.ArgumentParser(description=__doc__,
            formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--series", choices=sorted(SERIES), default="e12",
                    help="preferred-value series to draw from (default: e12 — "
                         "what a hobby parts drawer actually holds)")
    args = ap.parse_args()
    COL_POOL, ROW_POOL, RLOAD_POOL = make_pools(SERIES[args.series])

    print(f"Keypad base-4 ladder optimizer  [{args.series.upper()} values]"
          "  (10-bit ADC, ratiometric -> same for 3.3V or 5V)")
    results = []
    for name, wf in SCENARIOS:
        best = search(wf)
        results.append((name, wf, best))
    print("\nFRONTIER (wake strategy -> best achievable resolution):")
    print(f"{'scenario':<44} {'min gap':>8} {'k15 frac':>9}")
    for name, wf, best in results:
        if best:
            print(f"{name:<44} {best[0]:>8} {best[6][15]:>9.3f}")
    for name, wf, best in results:
        report(name, wf, best)

if __name__ == "__main__":
    main()
