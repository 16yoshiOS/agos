/* =============================================================================
 *  AGOS - PS/2 scancode decoder (kbd_decode.h / kbd_decode.c)
 *  Pure state machine: scancode set 1 bytes in, key events out. No hardware
 *  access, so it is unit-tested on the build host (tests/host_tests.c).
 *
 *  Set 1 is what the PC's 8042 controller delivers by default (it translates
 *  set 2 -> set 1), which is also what GRUB and QEMU leave in place.
 * ========================================================================== */
#ifndef AGOS_KBD_DECODE_H
#define AGOS_KBD_DECODE_H

#include <stdint.h>

/* Key codes. Printable keys and control characters use their ASCII value
 * (0x01..0x7F: '\n', '\b', '\t', 27 = ESC, Ctrl+A = 1, ...). Keys without an
 * ASCII value get codes >= 0x100. */
enum key_code {
    KEY_NONE  = 0,
    KEY_UP    = 0x101, KEY_DOWN  = 0x102, KEY_LEFT = 0x103, KEY_RIGHT = 0x104,
    KEY_HOME  = 0x105, KEY_END   = 0x106, KEY_PGUP = 0x107, KEY_PGDN  = 0x108,
    KEY_INS   = 0x109, KEY_DEL   = 0x10A,
    KEY_F1    = 0x111, KEY_F2    = 0x112, KEY_F3   = 0x113, KEY_F4    = 0x114,
    KEY_F5    = 0x115, KEY_F6    = 0x116, KEY_F7   = 0x117, KEY_F8    = 0x118,
    KEY_F9    = 0x119, KEY_F10   = 0x11A, KEY_F11  = 0x11B, KEY_F12   = 0x11C
};

/* Modifier flags reported in key_event.mods. */
#define KMOD_SHIFT 0x01
#define KMOD_CTRL  0x02
#define KMOD_ALT   0x04
#define KMOD_CAPS  0x08                 /* Caps Lock is currently ON          */

struct key_event {
    uint16_t keycode;                   /* ASCII char or enum key_code        */
    uint8_t  pressed;                   /* 1 = key down (or repeat), 0 = up   */
    uint8_t  mods;                      /* KMOD_* at the time of the event    */
    uint8_t  scancode;                  /* raw set-1 code, release bit removed*/
    uint8_t  extended;                  /* 1 if preceded by the 0xE0 prefix   */
};

struct kbd_decoder {
    uint8_t down;                       /* bitmask of held modifier keys      */
    uint8_t caps;                       /* Caps Lock toggle state             */
    uint8_t extended;                   /* previous byte was 0xE0             */
    uint8_t skip;                       /* bytes still to ignore (Pause key)  */
};

void kbd_decoder_init(struct kbd_decoder *d);

/* Feed one byte from port 0x60. Returns 1 and fills *ev when the byte
 * completes a key event; returns 0 for prefixes, pure modifier changes and
 * unknown keys. */
int kbd_decoder_feed(struct kbd_decoder *d, uint8_t scancode, struct key_event *ev);

#endif /* AGOS_KBD_DECODE_H */
