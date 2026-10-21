/* =============================================================================
 *  AGOS - Low-level CPU / port I/O helpers (io.h)
 *  Header-only, freestanding. Everything here is a thin wrapper over a single
 *  x86 instruction so the rest of the kernel stays free of inline assembly.
 * ========================================================================== */
#ifndef AGOS_IO_H
#define AGOS_IO_H

#include <stdint.h>

/* --- Port I/O ------------------------------------------------------------- */
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Tiny delay for slow legacy chips (PIC): a write to the unused POST port
 * 0x80 takes roughly 1 microsecond on every PC-compatible. */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

/* --- Interrupt flag --------------------------------------------------------- */
static inline void cpu_cli(void) { __asm__ volatile ("cli" : : : "memory"); }
static inline void cpu_sti(void) { __asm__ volatile ("sti" : : : "memory"); }
static inline void cpu_hlt(void) { __asm__ volatile ("hlt" : : : "memory"); }

/* Save EFLAGS and disable interrupts; restore with cpu_irq_restore(). Unlike
 * a bare cli/sti pair this is safe to nest and to call with IF already clear. */
static inline uint32_t cpu_irq_save(void)
{
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void cpu_irq_restore(uint32_t flags)
{
    __asm__ volatile ("push %0; popf" : : "r"(flags) : "memory", "cc");
}

/* Stop the machine for good: interrupts off, park the CPU. */
static inline __attribute__((noreturn)) void cpu_halt_forever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt" : : : "memory");
}

#endif /* AGOS_IO_H */
