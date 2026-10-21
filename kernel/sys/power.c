/* =============================================================================
 *  AGOS - Restart and power-off (sys/power.c)
 * ========================================================================== */
#include "power.h"
#include "io.h"

__attribute__((noreturn)) void power_restart(void)
{
    cpu_cli();

    /* 1. Pulse the CPU reset line through the 8042 keyboard controller. */
    for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++)
        ;
    outb(0x64, 0xFE);
    for (int i = 0; i < 100000; i++)
        io_wait();

    /* 2. Still here: load an empty IDT and take an interrupt -> triple fault. */
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) empty = { 0, 0 };

    __asm__ volatile ("lidt %0; int3" : : "m"(empty));
    cpu_halt_forever();
}

void power_shutdown(void)
{
    cpu_cli();
    outw(0x604, 0x2000);                 /* QEMU (q35 / piix4 PM)               */
    outw(0xB004, 0x2000);                /* Bochs, QEMU < 2.0                   */
    outw(0x4004, 0x3400);                /* VirtualBox                          */
}
