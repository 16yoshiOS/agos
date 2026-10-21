/* =============================================================================
 *  AGOS - PS/2 keyboard event queue (keyboard.h / keyboard.c)
 *  Scancodes arrive from ps2.c (IRQ1), are decoded by kbd_decode.c and queued
 *  as key events that the GUI drains with kbd_poll().
 * ========================================================================== */
#ifndef AGOS_KEYBOARD_H
#define AGOS_KEYBOARD_H

#include "kbd_decode.h"

/* Reset the decoder and the queue (no hardware access). */
void kbd_init(void);

/* Called from the IRQ handler for every byte the keyboard sends. */
void kbd_feed_byte(uint8_t scancode);

/* Pop the oldest event. Returns 1 and fills *ev, or 0 if the queue is empty. */
int kbd_poll(struct key_event *ev);

/* Number of events dropped because the queue was full (diagnostics). */
uint32_t kbd_dropped(void);

#endif /* AGOS_KEYBOARD_H */
