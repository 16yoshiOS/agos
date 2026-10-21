/* =============================================================================
 *  AGOS - S3 Vision 868 video driver (drivers/s3v.h / s3v.c)
 *  -----------------------------------------------------------------------------
 *  Chip: PCI vendor 0x5333 ("S3"), device 0x8811 (Vision 868 CRTC). The card
 *  exposes a linear framebuffer (LFB) aperture once its MMIO BAR is enabled;
 *  the classic Vision-era placement is the 0xA0000 VGA hole.
 *
 *  Pipeline:
 *    1. PCI probe (mechanism #1), size the LFB BAR, enable memory decoding.
 *    2. Program the CRT controller through ports 0x3D4/0x3D5 (standard CR00-
 *       CR1D plus the S3 extension registers CR31-CR4A reached in "extended
 *       mode"), the sequencer (0x3C4/0x3C5 incl. the S3 extended index), the
 *       graphics controller (0x3CE/0x3CF) and the attribute controller
 *       (0x3C0 with the 0x3DA flip-flop sync).
 *    3. Select packed-pixel 24-bpp or 32-bpp output and turn on the linear
 *       addressing bit so CPU writes go straight into VRAM.
 *    4. gfx_init_lfb() adopts the mapping; gfx_present() then does the
 *       back-buffer -> VRAM copy (rep movsd for 32 bpp, BGR packing for 24).
 *
 *  Mode policy for a hobby OS:
 *    - 1024x768x32 needs 3 MiB of VRAM, which no real Vision 868 board has
 *      (they ship 1-2 MiB); the driver therefore prefers 1024x768x24
 *      (2.25 MiB, fits a big-window board) and automatically falls back to
 *      640x480x24 (900 KiB) when the sized BAR says the big mode cannot fit.
 *    - On QEMU's std-vga emulation only 640x480 works; the fallback covers
 *      that too. MTRRs are not programmed because agos boots without paging
 *      and write-combining setup belongs to a future page-table stage.
 * ========================================================================== */
#ifndef AGOS_S3V_H
#define AGOS_S3V_H

#include <stdbool.h>
#include <stdint.h>

/* Probe an S3 Vision 868 and bring up its linear framebuffer. Tries w x h at
 * `bpp` (24 or 32), then 640x480x24 as fallback. Returns true if some mode
 * came up; the caller then hands s3v_lfb() to gfx_init_lfb(). */
bool s3v_init(int w, int h, int bpp);

bool     s3v_ready(void);                 /* chip found & programmed          */
void    *s3v_lfb(void);                   /* mapped LFB base (NULL = none)    */
int      s3v_width(void);
int      s3v_height(void);
uint32_t s3v_pitch(void);                 /* bytes per scanline               */
int      s3v_bpp(void);

/* Copy `rows` scanlines of XRGB source pixels into the VRAM LFB at `dst`.
 * 32 bpp uses rep movsd; 24 bpp packs BGR byte triples. For blits that skip
 * the RAM back buffer. */
void s3v_copy_rows(void *dst, const uint32_t *src, int w, int rows,
                   uint32_t dst_pitch);

#endif /* AGOS_S3V_H */
