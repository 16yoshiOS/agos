/* =============================================================================
 *  AGOS - Interrupt service routine dispatch (isr.h / isr.c)
 * ========================================================================== */
#ifndef AGOS_ISR_H
#define AGOS_ISR_H

#include <stdint.h>

#define ISR_EXCEPTIONS  32              /* vectors 0..31: CPU exceptions      */
#define IRQ_BASE        32              /* PIC IRQ 0..15 -> vectors 32..47    */
#define ISR_VECTORS     48
#define IRQ_VECTOR(n)   (IRQ_BASE + (n))

/* CPU state pushed by interrupt_stubs.asm. The order is fixed by the stubs;
 * do not reorder without changing the assembly. */
struct regs {
    uint32_t gs, fs, es, ds;                            /* pushed by isr_common */
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax; /* pusha             */
    uint32_t int_no, err_code;                          /* pushed by the stub   */
    uint32_t eip, cs, eflags;                           /* pushed by the CPU    */
    uint32_t useresp, ss;       /* valid ONLY on a ring 3 -> ring 0 transition  */
} __attribute__((packed));

_Static_assert(sizeof(struct regs) == 19 * 4, "struct regs must be 19 dwords");

typedef void (*isr_handler_t)(struct regs *r);

/* Register a handler for a vector (0..47). Passing NULL removes it. */
void isr_register_handler(uint8_t vector, isr_handler_t handler);

/* Called from isr_common (assembly). Do not call directly. */
void isr_dispatch(struct regs *r);

#endif /* AGOS_ISR_H */
