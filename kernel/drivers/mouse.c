/* =============================================================================
 *  AGOS - PS/2 mouse event queue (mouse.c)
 * ========================================================================== */
#include "mouse.h"
#include "pit.h"

#define QUEUE_SIZE 128                  /* power of two                        */
#define RESYNC_TICKS 5                  /* gap inside a packet that means "lost sync" */

static struct mouse_event queue[QUEUE_SIZE];
static volatile uint32_t  q_head, q_tail;   /* consumer / producer (IRQ)       */
static volatile uint32_t  dropped;
static struct mouse_decoder decoder;
static uint32_t last_byte_tick;

void mouse_setup(uint8_t device_id)
{
    mouse_decoder_init(&decoder, device_id);
    q_head = q_tail = 0;
    dropped = 0;
    last_byte_tick = 0;
}

void mouse_feed_byte(uint8_t byte)
{
    struct mouse_event ev;
    uint32_t now = pit_ticks();

    /* A packet that stalls halfway is garbage: start over. */
    if (decoder.n && (uint32_t)(now - last_byte_tick) > RESYNC_TICKS)
        mouse_decoder_reset(&decoder);
    last_byte_tick = now;

    if (!mouse_decoder_feed(&decoder, byte, &ev))
        return;

    uint32_t next = (q_tail + 1) & (QUEUE_SIZE - 1);
    if (next == q_head) {
        dropped++;
        return;
    }
    queue[q_tail] = ev;
    q_tail = next;
}

int mouse_poll(struct mouse_event *ev)
{
    if (q_head == q_tail)
        return 0;
    *ev = queue[q_head];
    q_head = (q_head + 1) & (QUEUE_SIZE - 1);
    return 1;
}

uint32_t mouse_dropped(void)   { return dropped; }
uint8_t  mouse_device_id(void) { return decoder.id; }
