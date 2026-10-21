/* =============================================================================
 *  AGOS - 8253/8254 PIT, channel 0 (pit.c)
 * -----------------------------------------------------------------------------
 *  Mode 2 (rate generator): the counter runs divisor..1 and reloads, firing
 *  IRQ0 once per period. Unlike mode 3 (square wave, which counts down by two)
 *  the counter value is directly "clocks left in this tick", which is what
 *  pit_clock_us() needs.
 * ========================================================================== */
#include "pit.h"
#include "io.h"
#include "isr.h"
#include "pic.h"

#define PIT_CH0_DATA  0x40
#define PIT_COMMAND   0x43
/* Command byte: channel 0 | access lobyte/hibyte | mode 2 (rate generator) | binary */
#define PIT_CMD_CH0_MODE2   0x34
/* Counter-latch command for channel 0. */
#define PIT_CMD_CH0_LATCH   0x00

static volatile uint32_t tick_count;
static uint32_t          tick_hz = PIT_TICK_HZ;
static uint32_t          divisor_now = 11932;

static void pit_irq_handler(struct regs *r)
{
    (void)r;
    tick_count++;
}

void pit_init(uint32_t hz)
{
    uint32_t divisor = pit_divisor_for(hz);

    divisor_now = divisor;
    tick_hz     = pit_rate_for_divisor(divisor); /* actual rate after rounding */
    tick_count  = 0;

    outb(PIT_COMMAND, PIT_CMD_CH0_MODE2);
    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    isr_register_handler(IRQ_VECTOR(0), pit_irq_handler);
    pic_clear_mask(0);
}

uint32_t pit_hz(void)    { return tick_hz; }
uint32_t pit_ticks(void) { return tick_count; }

uint32_t pit_uptime_ms(void)
{
    uint32_t t = tick_count;

    /* Split to avoid overflowing 32 bits in (t * 1000) after ~12 hours. */
    return (t / tick_hz) * 1000u + ((t % tick_hz) * 1000u) / tick_hz;
}

uint32_t pit_clock_us(void)
{
    static uint32_t last;
    uint32_t flags = cpu_irq_save();
    uint32_t ticks = tick_count;
    uint32_t lo, hi, count, elapsed, period_us, us;

    outb(PIT_COMMAND, PIT_CMD_CH0_LATCH);
    lo = inb(PIT_CH0_DATA);
    hi = inb(PIT_CH0_DATA);
    cpu_irq_restore(flags);

    count = (hi << 8) | lo;
    if (count > divisor_now || count == 0)
        count = divisor_now;
    elapsed = divisor_now - count;                  /* PIT clocks into this tick */

    /* One PIT clock = 0.838095 us ~= 3433/4096. */
    period_us = (divisor_now * 3433u) >> 12;
    us = ticks * period_us + ((elapsed * 3433u) >> 12);

    /* The counter can wrap just before the IRQ is serviced, which would make
     * the clock jump back by one period: never go backwards. */
    if ((int32_t)(us - last) < 0)
        us = last;
    last = us;
    return us;
}

void pit_sleep_ms(uint32_t ms)
{
    uint32_t wait   = (ms * tick_hz + 999u) / 1000u;    /* round up to ticks  */
    uint32_t start  = tick_count;

    /* Signed difference keeps this correct across tick-counter wrap-around. */
    while ((int32_t)(tick_count - start) < (int32_t)wait)
        cpu_hlt();
}
