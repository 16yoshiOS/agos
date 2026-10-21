/* =============================================================================
 *  AGOS - Interrupt Descriptor Table (idt.c)
 * ========================================================================== */
#include "idt.h"
#include "gdt.h"
#include "isr.h"
#include "lib.h"

extern void idt_flush(const struct idt_ptr *ip);    /* interrupt_stubs.asm */
extern uint32_t isr_stub_table[ISR_VECTORS];        /* interrupt_stubs.asm */

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idt_pointer;

void idt_encode(struct idt_entry *e, uint32_t handler,
                uint16_t selector, uint8_t type_attr)
{
    e->offset_low  = (uint16_t)(handler & 0xFFFF);
    e->selector    = selector;
    e->zero        = 0;
    e->type_attr   = type_attr;
    e->offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));                    /* all gates not-present */

    for (unsigned v = 0; v < ISR_VECTORS; v++)
        idt_encode(&idt[v], isr_stub_table[v], GDT_KERNEL_CODE,
                   IDT_GATE_INTERRUPT);

    idt_pointer.limit = (uint16_t)(sizeof(idt) - 1);
    idt_pointer.base  = (uint32_t)(uintptr_t)idt;
    idt_flush(&idt_pointer);
}
