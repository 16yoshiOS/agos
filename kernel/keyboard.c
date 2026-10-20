/* =============================================================================
 *  AGOS - PS/2 keyboard driver (keyboard.c)
 * ========================================================================== */
#include "keyboard.h"
#include "io.h"
#include "isr.h"
#include "pic.h"

#define KBD_DATA    0x60
#define KBD_STATUS  0x64
#define KBD_STATUS_OUTPUT_FULL 0x01

#define QUEUE_SIZE 64                   /* power of two */

/* Single-producer (IRQ1) / single-consumer (main loop) ring buffer. Indices
 * are only ever written by one side, so no locking is needed on x86. */
static struct key_event   queue[QUEUE_SIZE];
static volatile uint32_t  q_head;       /* next slot to read  (consumer)      */
static volatile uint32_t  q_tail;       /* next slot to write (IRQ handler)   */
static volatile uint32_t  dropped;
static struct kbd_decoder decoder;

static void kbd_irq_handler(struct regs *r)
{
    struct key_event ev;
    (void)r;

    uint8_t sc = inb(KBD_DATA);         /* must read, or the controller stalls */

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

void kbd_init(void)
{
    kbd_decoder_init(&decoder);
    q_head = q_tail = 0;

    /* Discard anything the firmware/bootloader left in the output buffer. */
    for (int i = 0; i < 16 && (inb(KBD_STATUS) & KBD_STATUS_OUTPUT_FULL); i++)
        (void)inb(KBD_DATA);

    isr_register_handler(IRQ_VECTOR(1), kbd_irq_handler);
    pic_clear_mask(1);
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
