/* =============================================================================
 *  AGOS - 8259A Programmable Interrupt Controller (pic.h / pic.c)
 * ========================================================================== */
#ifndef AGOS_PIC_H
#define AGOS_PIC_H

#include <stdint.h>

/* Remap the two PICs so IRQ 0..7 -> `master_base`.. and IRQ 8..15 ->
 * `slave_base`..; the BIOS default (0x08) collides with CPU exceptions.
 * All IRQ lines are left MASKED; drivers unmask their own. */
void pic_remap(uint8_t master_base, uint8_t slave_base);

void pic_set_mask(unsigned irq);        /* disable one IRQ line               */
void pic_clear_mask(unsigned irq);      /* enable one IRQ line (+cascade)     */

/* Acknowledge an IRQ (0..15). Slave IRQs need EOI on both chips. */
void pic_send_eoi(unsigned irq);

/* In-Service Register of both chips: bit n = IRQ n is being serviced. */
uint16_t pic_get_isr(void);

/* Returns 1 if `irq` was spurious (and has been dealt with: the caller must
 * not run a handler or send a normal EOI). */
int pic_handle_spurious(unsigned irq);

#endif /* AGOS_PIC_H */
