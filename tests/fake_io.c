/* =============================================================================
 *  AGOS - fake port-I/O model (tests/fake_io.c) - see fake_io.h
 * ========================================================================== */
#include "fake_io.h"

struct io_write io_log[IO_LOG_MAX];
int             io_log_n;
uint8_t         fake_isr_master, fake_isr_slave;
uint8_t         fake_mask_master = 0xFF, fake_mask_slave = 0xFF;
uint8_t         fake_kbd_bytes[256], fake_kbd_aux[256];
int             fake_kbd_count, fake_kbd_pos;
uint8_t         fake_cmos[128];
uint16_t        fake_pit_count = 1000;
int (*fake_in_hook)(uint16_t, int, uint32_t *);
int (*fake_out_hook)(uint16_t, int, uint32_t);

static uint8_t cmos_index;
static int     pit_latched, pit_hi_next;
static uint16_t pit_latch_val;

void outb(uint16_t port, uint8_t value)
{
    if (port == 0x80)
        return;
    if (fake_out_hook && fake_out_hook(port, 8, value))
        return;
    if (io_log_n < IO_LOG_MAX)
        io_log[io_log_n++] = (struct io_write){ port, value };
    if (port == 0x21) fake_mask_master = value;
    if (port == 0xA1) fake_mask_slave  = value;
    if (port == 0x70) cmos_index = value & 0x7F;
    if (port == 0x43 && value == 0x00) {            /* latch channel 0 count */
        pit_latched = 1;
        pit_hi_next = 0;
        pit_latch_val = fake_pit_count;
    }
}

uint8_t inb(uint16_t port)
{
    uint32_t v;

    if (fake_in_hook && fake_in_hook(port, 8, &v))
        return (uint8_t)v;
    switch (port) {
    case 0x60: return fake_kbd_pos < fake_kbd_count ? fake_kbd_bytes[fake_kbd_pos++] : 0;
    case 0x64:
        if (fake_kbd_pos < fake_kbd_count)
            return (uint8_t)(0x01 | (fake_kbd_aux[fake_kbd_pos] ? 0x20 : 0));
        return 0;
    case 0x21: return fake_mask_master;
    case 0xA1: return fake_mask_slave;
    case 0x20: return fake_isr_master;
    case 0xA0: return fake_isr_slave;
    case 0x71: return fake_cmos[cmos_index];
    case 0x40:
        if (pit_latched) {
            uint8_t b = pit_hi_next ? (uint8_t)(pit_latch_val >> 8) : (uint8_t)pit_latch_val;

            if (pit_hi_next)
                pit_latched = 0;
            pit_hi_next ^= 1;
            return b;
        }
        return 0;
    default:   return 0;
    }
}

void outw(uint16_t port, uint16_t value)
{
    if (fake_out_hook)
        fake_out_hook(port, 16, value);
}

uint16_t inw(uint16_t port)
{
    uint32_t v;

    if (fake_in_hook && fake_in_hook(port, 16, &v))
        return (uint16_t)v;
    return 0;
}

void outl(uint16_t port, uint32_t value)
{
    if (fake_out_hook)
        fake_out_hook(port, 32, value);
}

uint32_t inl(uint16_t port)
{
    uint32_t v;

    if (fake_in_hook && fake_in_hook(port, 32, &v))
        return v;
    return 0;
}
