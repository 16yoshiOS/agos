/* =============================================================================
 *  AGOS - Uncompressed image decoders (browser/image.h / image.c)
 *  -----------------------------------------------------------------------------
 *  "Stub" decoders for the <img> tag: they parse just enough of each header
 *  to find width/height and a raw pixel array, then normalise everything to
 *  32-bit XRGB rows so gfx_blit() can consume them.
 *
 *      BMP : BITMAPFILEHEADER + BITMAPINFOHEADER, 24-bpp uncompressed BI_RGB
 *            (bottom-up rows are flipped on the way in).
 *      PNG : IHDR chunk read for size; colour type 6 (RGBA8) with IDAT taken
 *            as STORED (uncompressed deflate blocks) — real inflate is out of
 *            scope, this is the honest stub. Colour types other than 6 fall
 *            back to a magenta placeholder of the right size.
 *      GIF : Logical Screen Descriptor for size; no LZW decode — placeholder.
 *
 *  Every entry point validates bounds before touching memory: garbage input
 *  returns false instead of faulting.
 * ========================================================================== */
#ifndef AGOS_IMAGE_H
#define AGOS_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct bitmap {
    int      w, h;              /* pixels                              */
    uint32_t *px;               /* w*h XRGB, row-major, top-down       */
    bool     placeholder;       /* true = synthetic checkerboard       */
};

/* Decode from a byte buffer. `out->px` points into caller-owned scratch
 * memory (`scratch`, `scratch_cap` bytes) — no malloc inside the decoder.
 * Returns false if the format is not recognised or does not fit. */
bool bmp_decode(const uint8_t *buf, size_t len, struct bitmap *out,
                uint32_t *scratch, size_t scratch_cap);
bool png_decode(const uint8_t *buf, size_t len, struct bitmap *out,
                uint32_t *scratch, size_t scratch_cap);
bool gif_decode(const uint8_t *buf, size_t len, struct bitmap *out,
                uint32_t *scratch, size_t scratch_cap);

/* One dispatcher: sniffs the magic bytes. */
bool image_decode(const uint8_t *buf, size_t len, struct bitmap *out,
                  uint32_t *scratch, size_t scratch_cap);

/* --- On-disk header layouts (packed, little/big endian noted) --------------- */

#pragma pack(push, 1)
struct bmp_file_header {          /* all little-endian                        */
    uint16_t type;                /* 'B'|'M'<<8 == 0x4D42                     */
    uint32_t file_size;
    uint16_t reserved1, reserved2;
    uint32_t pixel_offset;
};
struct bmp_info_header {
    uint32_t size;                /* 40 = BITMAPINFOHEADER                    */
    int32_t  width, height;       /* negative height = top-down               */
    uint16_t planes;
    uint16_t bpp;                 /* we support 24                            */
    uint32_t compression;         /* 0 = BI_RGB                               */
    uint32_t image_size;
    int32_t  xppm, yppm;
    uint32_t colors_used, colors_important;
};

struct png_ihdr {                 /* big-endian fields!                       */
    uint32_t width, height;
    uint8_t  bit_depth;           /* 8                                        */
    uint8_t  color_type;          /* 6 = RGBA                                 */
    uint8_t  compression, filter, interlace;
};

struct gif_lsd {                  /* little-endian                            */
    char     magic[6];            /* "GIF87a"/"GIF89a"                        */
    uint16_t screen_w, screen_h;
    uint8_t  packed;              /* global colour table flag etc.            */
    uint8_t  bg_index, aspect;
};
#pragma pack(pop)

#endif /* AGOS_IMAGE_H */
