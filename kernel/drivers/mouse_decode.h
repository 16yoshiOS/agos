/* =============================================================================
 *  AGOS - PS/2 mouse packet decoder (mouse_decode.h / mouse_decode.c)
 *  Pure state machine: bytes in, motion/button events out. No hardware access,
 *  so it is unit-tested on the build host.
 *
 *  Packet formats (device id from the GET_ID command):
 *    id 0  standard   3 bytes
 *    id 3  IntelliMouse (wheel)          4 bytes, byte 3 = signed Z
 *    id 4  IntelliMouse Explorer (5 btn) 4 bytes, byte 3 = Z (4 bit) + buttons 4,5
 *  Byte 0: YO XO YS XS 1 M R L ; byte 1: X delta ; byte 2: Y delta.
 * ========================================================================== */
#ifndef AGOS_MOUSE_DECODE_H
#define AGOS_MOUSE_DECODE_H

#include <stdint.h>

#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04
#define MOUSE_BTN_4      0x08
#define MOUSE_BTN_5      0x10

struct mouse_event {
    int16_t dx;                         /* + = right                           */
    int16_t dy;                         /* + = DOWN (PS/2 reports up; flipped) */
    int8_t  wheel;                      /* + = wheel towards the user (scroll down) */
    uint8_t buttons;                    /* MOUSE_BTN_* currently held          */
};

struct mouse_decoder {
    uint8_t buf[4];
    uint8_t n;                          /* bytes collected so far              */
    uint8_t plen;                       /* packet length: 3 or 4               */
    uint8_t id;                         /* device id                           */
};

void mouse_decoder_init(struct mouse_decoder *d, uint8_t device_id);
void mouse_decoder_reset(struct mouse_decoder *d);     /* drop a partial packet */

/* Feed one byte. Returns 1 and fills *ev when a packet is complete. A first
 * byte without bit 3 set cannot start a packet and is discarded (resync). */
int mouse_decoder_feed(struct mouse_decoder *d, uint8_t byte, struct mouse_event *ev);

#endif /* AGOS_MOUSE_DECODE_H */
