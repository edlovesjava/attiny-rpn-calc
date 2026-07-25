/*
 * ladder_test.ino — bench validation for the base-4 keypad resistor ladder.
 *
 * Decodes 16 keys on ONE ADC pin and reports, per press: raw ADC, decoded
 * keycode, the design-predicted ADC, the error, and the MARGIN to the nearest
 * decision boundary. The margin is the number that matters — it says how much
 * headroom is left before contact resistance / tolerance / noise cause a
 * misread.
 *
 * Circuit (see docs/keypad-ladder.md):
 *
 *         Rr                              Rc
 *   VCC ──┬──[  0R ]── Row1     Col1 ──[  0R ]──┬── SENSE
 *         ├──[5.6k ]── Row2     Col2 ──[1.1k ]──┤
 *         ├──[ 11k ]── Row3     Col3 ──[2.7k ]──┤
 *         └──[ 16k ]── Row4     Col4 ──[3.9k ]──┘
 *                                               ├──[39k ]── GND   (Rload)
 *      key (i,j) shorts Row_i to Col_j          ├──[10nF]── GND   (Csense)
 *      keycode = 4*i + j                        └─────────► SENSE_PIN
 *
 * Ratiometric: identical ADC codes at 3.3 V or 5 V, PROVIDED the ADC reference
 * is VCC and the ladder is powered from that same VCC.
 *
 * Target: any 10-bit AVR (Uno / Nano / Pro Mini). For a 12-bit ADC, scale both
 * tables by 4 — and note ESP32's ADC is nonlinear, so re-measure rather than
 * trusting the scaled numbers.
 */

const uint8_t SENSE_PIN = A0;

/* Decision thresholds: midpoints between adjacent keycodes.
 * ADC codes DESCEND as keycode ascends (more resistance -> less voltage).
 * ADC >= KEY_THRESH[k]  =>  at least keycode k.
 * KEY_THRESH[15] doubles as the idle/press boundary and sits at ~0.5*VCC,
 * matching the PCINT digital-HIGH threshold used for wake in the real module. */
static const uint16_t KEY_THRESH[16] = {
  1009, 976, 943, 912, 884, 858, 833, 810,
   789, 769, 748, 732, 718, 701, 684, 507
};

/* Design-predicted centres, for error reporting only. */
static const uint16_t KEY_ADC[16] = {
  1023, 995, 957, 930, 895, 873, 843, 823,
   798, 781, 757, 740, 725, 711, 691, 677
};

#define KEY_NONE   0xFF
#define STABLE_N   3      /* consecutive agreeing decodes before we believe it */

uint8_t decode_key(uint16_t adc) {
  if (adc < KEY_THRESH[15]) return KEY_NONE;   /* idle / no press */
  for (uint8_t k = 0; k < 16; k++)             /* first (smallest) k that fits */
    if (adc >= KEY_THRESH[k]) return k;
  return KEY_NONE;
}

/* Source impedance peaks at 13.2k (keycode 15), so the first conversion after
 * touching the pin is unreliable — discard it, then average 8. */
uint16_t read_sense(void) {
  analogRead(SENSE_PIN);
  uint16_t acc = 0;
  for (uint8_t i = 0; i < 8; i++) acc += analogRead(SENSE_PIN);
  return (acc + 4) >> 3;
}

void setup(void) {
  Serial.begin(115200);
  analogReference(DEFAULT);   /* = AVcc. NEVER INTERNAL (1.1V bandgap) — the
                               * whole scheme depends on VCC and the ADC ref
                               * scaling together. */
  Serial.println(F("adc\tkey\tr,c\tdesign\terr\tmargin"));
}

void loop(void) {
  static uint8_t stable = KEY_NONE, cand = KEY_NONE, n = 0;

  uint16_t adc = read_sense();
  uint8_t  k   = decode_key(adc);

  if (k == cand) { if (n < 255) n++; } else { cand = k; n = 1; }
  if (n < STABLE_N || cand == stable) { delay(2); return; }
  stable = cand;

  if (stable == KEY_NONE) { Serial.println(F("-- idle --")); delay(2); return; }

  /* Distance to the nearest decision boundary, in ADC counts. */
  int lo = (int)adc - (int)KEY_THRESH[stable];
  int hi = (stable == 0) ? 999 : (int)KEY_THRESH[stable - 1] - 1 - (int)adc;
  int margin = (lo < hi) ? lo : hi;

  Serial.print(adc);                              Serial.print('\t');
  Serial.print(stable);                           Serial.print('\t');
  Serial.print(stable >> 2);                      Serial.print(',');
  Serial.print(stable & 3);                       Serial.print('\t');
  Serial.print(KEY_ADC[stable]);                  Serial.print('\t');
  Serial.print((int)adc - (int)KEY_ADC[stable]);  Serial.print('\t');
  Serial.println(margin);
  delay(2);
}
