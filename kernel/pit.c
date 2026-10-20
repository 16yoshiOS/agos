/* =============================================================================
 *  AGOS - 8253/8254 PIT, channel 0 (pit.c)
 * ========================================================================== */
#include "pit.h"
#include "io.h"
#include "isr.h"
#include "pic.h"

#define PIT_CH0_DATA  0x40
#define PIT_COMMAND   0x43
/* Command byte: channel 0 | access lobyte/hibyte | mode 3 (square wave) | binary */
#define PIT_CMD_CH0_MODE3  0x36

static volatile uint32_t tick_count;
static uint32_t          tick_hz = PIT_TICK_HZ;

static void pit_irq_handler(struct regs *r)
{
    (void)r;
    tick_count++;
}

void pit_init(uint32_t hz)
{
    uint32_t divisor = pit_divisor_for(hz);

    tick_hz    = pit_rate_for_divisor(divisor); /* actual rate after rounding */
    tick_count = 0;

    outb(PIT_COMMAND, PIT_CMD_CH0_MODE3);
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

void pit_sleep_ms(uint32_t ms)
{
    uint32_t wait   = (ms * tick_hz + 999u) / 1000u;    /* round up to ticks  */
    uint32_t start  = tick_count;

    /* Signed difference keeps this correct across tick-counter wrap-around. */
    while ((int32_t)(tick_count - start) < (int32_t)wait)
        cpu_hlt();
}
