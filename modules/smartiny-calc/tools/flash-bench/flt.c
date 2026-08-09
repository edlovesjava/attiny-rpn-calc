/* Matched-harness float comparison: same sinks, same shape as num.c */
#include <stdint.h>
#include <stdlib.h>

volatile uint8_t sink;
volatile float   vin_a, vin_b;
char             buf[24];

int main(void)
{
    float a = vin_a, b = vin_b;
#if STAGE >= 1
    dtostrf(a, 10, 6, buf);
    sink = (uint8_t)buf[0];
#endif
#if STAGE >= 2
    vin_a = a + b;
    vin_b = a - b;
#endif
#if STAGE >= 3
    vin_a = a * b;
#endif
#if STAGE >= 4
    vin_b = a / b;
#endif
    for (;;) sink++;
}
