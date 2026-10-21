/* =============================================================================
 *  AGOS - Linear-framebuffer graphics layer, double buffered (gfx.h / gfx.c)
 *  All drawing goes to an off-screen back buffer; gfx_present() copies it to
 *  the real framebuffer. Colours are passed as 0xRRGGBB and packed into the
 *  display's actual pixel format internally.
 * ========================================================================== */
#ifndef AGOS_GFX_H
#define AGOS_GFX_H

#include <stdbool.h>
#include <stdint.h>
#include "multiboot.h"

/* Initialise from the bootloader's framebuffer description. Returns false (and
 * leaves graphics disabled) if there is no usable 32-bpp direct-colour mode. */
bool gfx_init(const struct multiboot_info *mbi);
bool gfx_ready(void);

int  gfx_width(void);
int  gfx_height(void);

void gfx_clear(uint32_t rgb);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_stroke_rect(int x, int y, int w, int h, uint32_t rgb);   /* 1-px frame */

/* Text uses the built-in 8x8 font; each font pixel becomes scale x scale. */
void gfx_draw_char(int x, int y, char ch, uint32_t rgb, int scale);
void gfx_draw_string(int x, int y, const char *s, uint32_t rgb, int scale);
int  gfx_string_width(const char *s, int scale);

/* Copy the back buffer to the screen. */
void gfx_present(void);

#endif /* AGOS_GFX_H */
