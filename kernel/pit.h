/* =============================================================================
 *  AGOS - 8253/8254 Programmable Interval Timer, channel 0 (pit.h / pit.c)
 *  Drives the system tick on IRQ0. Default AGOS tick rate: 100 Hz.
 * ========================================================================== */
#ifndef AGOS_PIT_H
#define AGOS_PIT_H

#include <stdint.h>

#define PIT_BASE_HZ  1193182u           /* input clock of the PIT             */
#define PIT_TICK_HZ  100u               /* AGOS system tick                   */

/* Divisor for a target frequency, rounded to nearest and clamped to the 16-bit
 * counter range (a programmed value of 0 means 65536, avoided here). */
static inline uint32_t pit_divisor_for(uint32_t hz)
{
    uint32_t d;

    if (hz == 0)
        hz = 1;
    d = (PIT_BASE_HZ + hz / 2) / hz;
    if (d < 1)
        d = 1;
    if (d > 65535)
        d = 65535;
    return d;
}

/* Actual tick rate for a divisor, rounded to the nearest Hz (the default
 * divisor 11932 gives 99.998 Hz; truncating would report 99 and make the
 * uptime clock run 1% slow). */
static inline uint32_t pit_rate_for_divisor(uint32_t divisor)
{
    return (PIT_BASE_HZ + divisor / 2) / divisor;
}

/* Program channel 0 and start counting ticks. The IDT, PIC remap and STI must
 * be done by the caller. */
void pit_init(uint32_t hz);

uint32_t pit_hz(void);                  /* configured tick rate               */
uint32_t pit_ticks(void);               /* ticks since pit_init()             */
uint32_t pit_uptime_ms(void);           /* milliseconds since pit_init()      */

/* Sleep using HLT; interrupts must be enabled. Resolution = one tick. */
void pit_sleep_ms(uint32_t ms);

#endif /* AGOS_PIT_H */
