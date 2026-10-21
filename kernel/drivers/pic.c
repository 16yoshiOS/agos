/* =============================================================================
 *  AGOS - 8259A Programmable Interrupt Controller (pic.c)
 * ========================================================================== */
#include "pic.h"
#include "io.h"

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define PIC_EOI        0x20
#define PIC_READ_ISR   0x0B             /* OCW3: next read of CMD returns ISR */

#define ICW1_INIT      0x10
#define ICW1_ICW4      0x01             /* ICW4 will follow                   */
#define ICW4_8086      0x01             /* 8086/88 mode                       */

void pic_remap(uint8_t master_base, uint8_t slave_base)
{
    /* ICW1: start the init sequence (cascade mode, expect ICW4). The legacy
     * chips are slow, hence io_wait() between writes. */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);  io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);  io_wait();

    /* ICW2: vector offsets. */
    outb(PIC1_DATA, master_base);           io_wait();
    outb(PIC2_DATA, slave_base);            io_wait();

    /* ICW3: master has the slave on IRQ2 (bit mask 0b100); the slave's
     * cascade identity is 2. */
    outb(PIC1_DATA, 1 << 2);                io_wait();
    outb(PIC2_DATA, 2);                     io_wait();

    /* ICW4: 8086 mode. */
    outb(PIC1_DATA, ICW4_8086);             io_wait();
    outb(PIC2_DATA, ICW4_8086);             io_wait();

    /* Mask everything until a driver asks for its line. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_set_mask(unsigned irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (uint8_t)(1u << (irq & 7));

    outb(port, inb(port) | bit);
}

void pic_clear_mask(unsigned irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (uint8_t)(1u << (irq & 7));

    outb(port, inb(port) & (uint8_t)~bit);

    /* Slave IRQs are only delivered if the cascade line (IRQ2) is open. */
    if (irq >= 8)
        outb(PIC1_DATA, inb(PIC1_DATA) & (uint8_t)~(1u << 2));
}

void pic_send_eoi(unsigned irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

uint16_t pic_get_isr(void)
{
    outb(PIC1_CMD, PIC_READ_ISR);
    outb(PIC2_CMD, PIC_READ_ISR);
    return (uint16_t)((inb(PIC2_CMD) << 8) | inb(PIC1_CMD));
}

int pic_handle_spurious(unsigned irq)
{
    /* A spurious IRQ 7 (master) / 15 (slave) shows up as "line 7 of that chip
     * is not actually in service". */
    if (irq == 7) {
        if (!(pic_get_isr() & (1u << 7)))
            return 1;                   /* no EOI for a spurious master IRQ   */
    } else if (irq == 15) {
        if (!(pic_get_isr() & (1u << 15))) {
            outb(PIC1_CMD, PIC_EOI);    /* master saw the cascade IRQ2: ack   */
            return 1;
        }
    }
    return 0;
}
