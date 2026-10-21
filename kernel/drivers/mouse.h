/* =============================================================================
 *  AGOS - PS/2 mouse event queue (mouse.h / mouse.c)
 *  Bytes arrive from ps2.c (IRQ12); decoded events are queued for the GUI.
 * ========================================================================== */
#ifndef AGOS_MOUSE_H
#define AGOS_MOUSE_H

#include <stdbool.h>
#include "mouse_decode.h"

/* Forget everything and expect packets for the given device id. */
void mouse_setup(uint8_t device_id);

/* Called from the IRQ handler for every byte the mouse sends. */
void mouse_feed_byte(uint8_t byte);

/* Pop the oldest event; returns 0 when the queue is empty. */
int  mouse_poll(struct mouse_event *ev);

uint32_t mouse_dropped(void);           /* events lost to a full queue         */
uint8_t  mouse_device_id(void);

#endif /* AGOS_MOUSE_H */
