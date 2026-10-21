/* =============================================================================
 *  AGOS - Kernel panic (panic.h / panic.c)
 * ========================================================================== */
#ifndef AGOS_PANIC_H
#define AGOS_PANIC_H

#include "isr.h"

/* Fatal CPU exception: log registers to serial, draw the panic screen
 * (graphics if available, else VGA text) and halt. */
__attribute__((noreturn)) void kernel_panic_regs(const struct regs *r);

/* Fatal kernel error with a message. */
__attribute__((noreturn)) void kernel_panic(const char *message);

#endif /* AGOS_PANIC_H */
