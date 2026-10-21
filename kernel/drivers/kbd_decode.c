/* =============================================================================
 *  AGOS - PS/2 scancode decoder, set 1, US layout (kbd_decode.c)
 * ========================================================================== */
#include "kbd_decode.h"

/* Bits of kbd_decoder.down */
#define DN_LSHIFT 0x01
#define DN_RSHIFT 0x02
#define DN_LCTRL  0x04
#define DN_RCTRL  0x08
#define DN_LALT   0x10
#define DN_RALT   0x20

#define SC_MAP_LEN 58                   /* scancodes 0x00..0x39 (space)       */

/* Scancode -> ASCII. Index = make code. 0 = no printable meaning (modifiers,
 * unused). Each literal is annotated with the scancode range it covers. */
static const char map_normal[64] =
    "\0\x1b"                            /* 0x00-0x01  -, ESC                  */
    "1234567890-=\b\t"                  /* 0x02-0x0F                          */
    "qwertyuiop[]\n\0"                  /* 0x10-0x1D  ..., Enter, LCtrl       */
    "asdfghjkl;'`\0\\"                  /* 0x1E-0x2B  ..., LShift, backslash  */
    "zxcvbnm,./\0*\0 ";                 /* 0x2C-0x39  ..., RShift, KP*, LAlt, Space */

static const char map_shift[64] =
    "\0\x1b"
    "!@#$%^&*()_+\b\t"
    "QWERTYUIOP{}\n\0"
    "ASDFGHJKL:\"~\0|"
    "ZXCVBNM<>?\0*\0 ";

_Static_assert(sizeof(map_normal) >= SC_MAP_LEN, "scancode map too short");

void kbd_decoder_init(struct kbd_decoder *d)
{
    d->down = 0;
    d->caps = 0;
    d->extended = 0;
    d->skip = 0;
}

static void set_down(struct kbd_decoder *d, uint8_t bit, int pressed)
{
    if (pressed)
        d->down |= bit;
    else
        d->down &= (uint8_t)~bit;
}

static uint8_t current_mods(const struct kbd_decoder *d)
{
    uint8_t m = 0;

    if (d->down & (DN_LSHIFT | DN_RSHIFT)) m |= KMOD_SHIFT;
    if (d->down & (DN_LCTRL  | DN_RCTRL))  m |= KMOD_CTRL;
    if (d->down & (DN_LALT   | DN_RALT))   m |= KMOD_ALT;
    if (d->caps)                           m |= KMOD_CAPS;
    return m;
}

int kbd_decoder_feed(struct kbd_decoder *d, uint8_t sc, struct key_event *ev)
{
    int      extended, pressed;
    uint8_t  code;
    uint16_t key = KEY_NONE;

    if (d->skip) {                      /* tail of a Pause/Break sequence     */
        d->skip--;
        return 0;
    }
    if (sc == 0xE0) {                   /* extended-key prefix                */
        d->extended = 1;
        return 0;
    }
    if (sc == 0xE1) {                   /* Pause/Break: E1 1D 45 E1 9D C5     */
        d->extended = 0;
        d->skip = 5;
        return 0;
    }
    if (sc == 0x00 || sc == 0xFF)       /* controller overrun / error marker  */
        return 0;

    extended   = d->extended;
    d->extended = 0;
    pressed    = !(sc & 0x80);
    code       = sc & 0x7F;

    if (extended) {
        switch (code) {
        case 0x1D: set_down(d, DN_RCTRL, pressed); return 0;
        case 0x38: set_down(d, DN_RALT,  pressed); return 0;   /* AltGr */
        case 0x2A: case 0x36:           /* "fake shift" around PrtSc / Ins    */
            return 0;
        case 0x48: key = KEY_UP;    break;
        case 0x50: key = KEY_DOWN;  break;
        case 0x4B: key = KEY_LEFT;  break;
        case 0x4D: key = KEY_RIGHT; break;
        case 0x47: key = KEY_HOME;  break;
        case 0x4F: key = KEY_END;   break;
        case 0x49: key = KEY_PGUP;  break;
        case 0x51: key = KEY_PGDN;  break;
        case 0x52: key = KEY_INS;   break;
        case 0x53: key = KEY_DEL;   break;
        case 0x1C: key = '\n';      break;                     /* keypad Enter */
        case 0x35: key = '/';       break;                     /* keypad /     */
        default:   return 0;
        }
    } else {
        switch (code) {
        case 0x2A: set_down(d, DN_LSHIFT, pressed); return 0;
        case 0x36: set_down(d, DN_RSHIFT, pressed); return 0;
        case 0x1D: set_down(d, DN_LCTRL,  pressed); return 0;
        case 0x38: set_down(d, DN_LALT,   pressed); return 0;
        case 0x3A:                                              /* Caps Lock */
            if (pressed)
                d->caps ^= 1;
            return 0;
        default:
            break;
        }

        if (code >= 0x3B && code <= 0x44) {
            key = (uint16_t)(KEY_F1 + (code - 0x3B));
        } else if (code == 0x57) {
            key = KEY_F11;
        } else if (code == 0x58) {
            key = KEY_F12;
        } else if (code < SC_MAP_LEN) {
            int shift = (d->down & (DN_LSHIFT | DN_RSHIFT)) != 0;
            char ch   = map_normal[code];

            if (ch == 0)
                return 0;
            if (ch >= 'a' && ch <= 'z') {
                if (shift ^ d->caps)    /* Caps Lock only affects letters     */
                    ch = (char)(ch - 'a' + 'A');
            } else if (shift) {
                ch = map_shift[code];
            }
            if ((d->down & (DN_LCTRL | DN_RCTRL)) &&
                ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')))
                ch = (char)((ch & 0x1F));   /* Ctrl+A..Z -> 0x01..0x1A       */
            key = (uint8_t)ch;
        } else {
            return 0;                   /* keypad digits etc.: not mapped yet */
        }
    }

    ev->keycode  = key;
    ev->pressed  = (uint8_t)pressed;
    ev->mods     = current_mods(d);
    ev->scancode = code;
    ev->extended = (uint8_t)extended;
    return 1;
}
