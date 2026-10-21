/* =============================================================================
 *  AGOS - Interrupt service routine dispatch (isr.c)
 * -----------------------------------------------------------------------------
 *  Every vector enters through a stub in interrupt_stubs.asm, which calls
 *  isr_dispatch(). Exceptions without a registered handler are fatal and go to
 *  the panic screen. Hardware IRQs are acknowledged here, after the handler, so
 *  individual drivers never touch the PIC.
 * ========================================================================== */
#include "isr.h"
#include "panic.h"
#include "pic.h"

static isr_handler_t handlers[ISR_VECTORS];

void isr_register_handler(uint8_t vector, isr_handler_t handler)
{
    if (vector < ISR_VECTORS)
        handlers[vector] = handler;
}

void isr_dispatch(struct regs *r)
{
    uint32_t vec = r->int_no;

    if (vec < ISR_EXCEPTIONS) {                 /* CPU exception              */
        if (handlers[vec])
            handlers[vec](r);
        else
            kernel_panic_regs(r);               /* never returns              */
        return;
    }

    if (vec < ISR_VECTORS) {                    /* hardware IRQ               */
        unsigned irq = vec - IRQ_BASE;

        /* IRQ 7 / 15 can fire spuriously; those must NOT be acknowledged
         * (except the master side of a spurious IRQ 15). */
        if (pic_handle_spurious(irq))
            return;

        if (handlers[vec])
            handlers[vec](r);
        pic_send_eoi(irq);
    }
}
