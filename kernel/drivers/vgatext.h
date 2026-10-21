/* =============================================================================
 *  AGOS - VGA text-mode fallback (vgatext.h / vgatext.c)
 *  80x25 text at 0xB8000. Used only when no linear framebuffer is available
 *  (bootloader could not set a graphics mode, or QEMU's plain -kernel loader).
 * ========================================================================== */
#ifndef AGOS_VGATEXT_H
#define AGOS_VGATEXT_H

#include <stdint.h>

#define VGATEXT_COLS 80
#define VGATEXT_ROWS 25

/* Attribute = (background << 4) | foreground, standard 16-colour VGA palette. */
#define VGATEXT_ATTR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

void vgatext_clear(uint8_t attr);
void vgatext_puts(int col, int row, const char *s, uint8_t attr);

#endif /* AGOS_VGATEXT_H */
