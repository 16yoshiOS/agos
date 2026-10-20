/* =============================================================================
 *  AGOS - PS/2 keyboard driver (keyboard.h / keyboard.c)
 *  IRQ1 handler -> scancode decoder -> small ring buffer of key events that the
 *  main loop (later: the window manager) drains with kbd_poll().
 * ========================================================================== */
#ifndef AGOS_KEYBOARD_H
#define AGOS_KEYBOARD_H

#include "kbd_decode.h"

/* Flush the controller, install the IRQ1 handler and unmask IRQ1. */
void kbd_init(void);

/* Pop the oldest event. Returns 1 and fills *ev, or 0 if the queue is empty. */
int kbd_poll(struct key_event *ev);

/* Number of events dropped because the queue was full (diagnostics). */
uint32_t kbd_dropped(void);

#endif /* AGOS_KEYBOARD_H */
