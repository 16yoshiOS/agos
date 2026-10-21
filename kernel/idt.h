/* =============================================================================
 *  AGOS - Interrupt Descriptor Table (idt.h / idt.c)
 * ========================================================================== */
#ifndef AGOS_IDT_H
#define AGOS_IDT_H

#include <stdint.h>

#define IDT_ENTRIES 256

/* type_attr: P | DPL | 0 | gate type. 0x8E = present, ring 0, 32-bit
 * INTERRUPT gate (the CPU clears IF on entry, so handlers cannot be nested by
 * accident). */
#define IDT_GATE_INTERRUPT 0x8E

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct idt_entry) == 8, "IDT entry must be 8 bytes");
_Static_assert(sizeof(struct idt_ptr)   == 6, "IDTR image must be 6 bytes");

/* Fill one gate. Pure function (no hardware access). */
void idt_encode(struct idt_entry *e, uint32_t handler,
                uint16_t selector, uint8_t type_attr);

/* Install gates for vectors 0..47 (exceptions + remapped IRQs) and load IDTR.
 * Vectors 48..255 stay not-present. */
void idt_init(void);

#endif /* AGOS_IDT_H */
