/* =============================================================================
 *  AGOS - PS/2 controller (8042) driver (ps2.h / ps2.c)
 *  Initialises the controller, the keyboard and the mouse, and routes the one
 *  shared data port to the right consumer (keyboard.c / mouse.c) from IRQ1 and
 *  IRQ12.
 * ========================================================================== */
#ifndef AGOS_PS2_H
#define AGOS_PS2_H

#include <stdbool.h>
#include <stdint.h>

/* Bring up the controller and both devices. Call after pic_remap() and
 * before enabling interrupts. Always installs the keyboard handler; the mouse
 * is only enabled if it answers. */
void ps2_init(void);

bool    ps2_mouse_present(void);
uint8_t ps2_mouse_id(void);             /* 0 standard, 3 wheel, 4 five-button  */

/* Drain the controller's output buffer, routing each byte by its source.
 * Called from the IRQ handlers (and by tests). */
void ps2_service(void);

#endif /* AGOS_PS2_H */
