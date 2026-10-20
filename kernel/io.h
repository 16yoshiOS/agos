/* =============================================================================
 *  AGOS - Low-level CPU / port I/O helpers (io.h)
 *  Header-only, freestanding. Everything here is a thin wrapper over a single
 *  x86 instruction so the rest of the kernel stays free of inline assembly.
 * ========================================================================== */
#ifndef AGOS_IO_H
#define AGOS_IO_H

#include <stdint.h>

/* Write one byte to an I/O port. */
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* Read one byte from an I/O port. */
static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Tiny delay for slow legacy chips (PIC): a write to the unused POST port
 * 0x80 takes roughly 1 microsecond on every PC-compatible. */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void cpu_cli(void) { __asm__ volatile ("cli" : : : "memory"); }
static inline void cpu_sti(void) { __asm__ volatile ("sti" : : : "memory"); }
static inline void cpu_hlt(void) { __asm__ volatile ("hlt" : : : "memory"); }

/* Stop the machine for good: interrupts off, park the CPU. */
static inline __attribute__((noreturn)) void cpu_halt_forever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt" : : : "memory");
}

#endif /* AGOS_IO_H */
