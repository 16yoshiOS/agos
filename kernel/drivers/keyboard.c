/* =============================================================================
 *  AGOS - PS/2 keyboard event queue (keyboard.c)
 * ========================================================================== */
#include "keyboard.h"

#define QUEUE_SIZE 64                   /* power of two */

/* Single-producer (IRQ) / single-consumer (main loop) ring buffer. Indices
 * are only ever written by one side, so no locking is needed on x86. */
static struct key_event   queue[QUEUE_SIZE];
static volatile uint32_t  q_head;       /* next slot to read  (consumer)      */
static volatile uint32_t  q_tail;       /* next slot to write (IRQ handler)   */
static volatile uint32_t  dropped;
static struct kbd_decoder decoder;

void kbd_init(void)
{
    kbd_decoder_init(&decoder);
    q_head = q_tail = 0;
    dropped = 0;
}

void kbd_feed_byte(uint8_t sc)
{
    struct key_event ev;

    if (!kbd_decoder_feed(&decoder, sc, &ev))
        return;

    uint32_t next = (q_tail + 1) & (QUEUE_SIZE - 1);
    if (next == q_head) {               /* full: drop the newest event        */
        dropped++;
        return;
    }
    queue[q_tail] = ev;
    q_tail = next;
}

int kbd_poll(struct key_event *ev)
{
    if (q_head == q_tail)
        return 0;
    *ev = queue[q_head];
    q_head = (q_head + 1) & (QUEUE_SIZE - 1);
    return 1;
}

uint32_t kbd_dropped(void)
{
    return dropped;
}
