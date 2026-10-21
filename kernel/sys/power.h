/* =============================================================================
 *  AGOS - Restart and power-off (sys/power.h, sys/power.c)
 * ========================================================================== */
#ifndef AGOS_POWER_H
#define AGOS_POWER_H

/* Reboot the machine (keyboard-controller reset, then a deliberate triple
 * fault). Does not return. */
__attribute__((noreturn)) void power_restart(void);

/* Ask the virtual machine to power off (QEMU, Bochs, VirtualBox exit ports).
 * Returns if none of them worked - real hardware needs ACPI, which AGOS does
 * not parse yet - and the caller shows a "safe to switch off" screen. */
void power_shutdown(void);

#endif /* AGOS_POWER_H */
