/* =============================================================================
 *  AGOS - Built-in 8x8 bitmap font (font.h / font.c)
 * ========================================================================== */
#ifndef AGOS_FONT_H
#define AGOS_FONT_H

#include <stdint.h>

#define FONT_W 8
#define FONT_H 8

/* Returns the 8 row bytes of the glyph for `ch`. Bit 0 of each byte is the
 * LEFTMOST pixel. Characters outside printable ASCII (0x20-0x7E) yield a
 * hollow-box "missing glyph" so gaps are visible instead of silently blank. */
const uint8_t *font_glyph(char ch);

#endif /* AGOS_FONT_H */
