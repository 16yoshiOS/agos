/* =============================================================================
 *  AGOS - Linear-framebuffer graphics layer (gfx.c)
 * -----------------------------------------------------------------------------
 *  The framebuffer address comes from GRUB's multiboot_info. Paging is off, so
 *  the physical address is directly usable. The back buffer lives in .bss and
 *  stores pixels already packed for the screen, so present is a straight copy.
 * ========================================================================== */
#include "gfx.h"
#include "font.h"
#include "lib.h"

/* Largest mode we support: 1920x1080 (8 MiB back buffer in .bss; .bss costs
 * nothing in the ELF/ISO and GRUB zero-fills it). */
#define GFX_MAX_PIXELS (1920u * 1080u)

static uint32_t backbuffer[GFX_MAX_PIXELS];

static struct {
    bool      ready;
    uint8_t  *fb;                       /* physical framebuffer               */
    uint32_t  pitch;                    /* bytes per scanline of the screen   */
    int       w, h;
    uint8_t   rpos, rsize, gpos, gsize, bpos, bsize;
} g;

bool gfx_init(const struct multiboot_info *mbi)
{
    g.ready = false;

    if (!(mbi->flags & MBI_FLAG_FRAMEBUF))
        return false;
    if (mbi->framebuffer_type != MB_FB_TYPE_RGB || mbi->framebuffer_bpp != 32)
        return false;
    if (mbi->framebuffer_addr_hi != 0)  /* above 4 GiB: not addressable here  */
        return false;

    uint32_t w = mbi->framebuffer_width;
    uint32_t h = mbi->framebuffer_height;

    if (w == 0 || h == 0 || w > 1920 || h > 1080 || w * h > GFX_MAX_PIXELS)
        return false;
    if (mbi->framebuffer_pitch < w * 4)
        return false;
    if (mbi->red_mask_size > 8 || mbi->green_mask_size > 8 || mbi->blue_mask_size > 8)
        return false;

    g.fb    = (uint8_t *)(uintptr_t)mbi->framebuffer_addr_lo;
    g.pitch = mbi->framebuffer_pitch;
    g.w     = (int)w;
    g.h     = (int)h;
    g.rpos  = mbi->red_field_position;   g.rsize = mbi->red_mask_size;
    g.gpos  = mbi->green_field_position; g.gsize = mbi->green_mask_size;
    g.bpos  = mbi->blue_field_position;  g.bsize = mbi->blue_mask_size;
    g.ready = true;
    return true;
}

bool gfx_ready(void)  { return g.ready; }
int  gfx_width(void)  { return g.w; }
int  gfx_height(void) { return g.h; }

/* 0xRRGGBB -> the screen's native pixel layout. */
static uint32_t pack(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t gr = (rgb >> 8) & 0xFF;
    uint32_t b = rgb & 0xFF;

    return ((r  >> (8 - g.rsize)) << g.rpos) |
           ((gr >> (8 - g.gsize)) << g.gpos) |
           ((b  >> (8 - g.bsize)) << g.bpos);
}

/* Fill n dwords with v. REP STOSD instead of a C loop: faster, and cannot be
 * turned into a recursive memset call by the compiler. */
static void fill32(uint32_t *dst, uint32_t v, size_t n)
{
    __asm__ volatile ("cld; rep stosl"
                      : "+D"(dst), "+c"(n)
                      : "a"(v)
                      : "memory");
}

void gfx_clear(uint32_t rgb)
{
    if (!g.ready)
        return;
    fill32(backbuffer, pack(rgb), (size_t)g.w * (size_t)g.h);
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb)
{
    if (!g.ready)
        return;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g.w ? g.w : x + w;
    int y1 = y + h > g.h ? g.h : y + h;

    if (x0 >= x1 || y0 >= y1)
        return;

    uint32_t px = pack(rgb);
    for (int row = y0; row < y1; row++)
        fill32(&backbuffer[row * g.w + x0], px, (size_t)(x1 - x0));
}

void gfx_stroke_rect(int x, int y, int w, int h, uint32_t rgb)
{
    gfx_fill_rect(x,         y,         w, 1, rgb);
    gfx_fill_rect(x,         y + h - 1, w, 1, rgb);
    gfx_fill_rect(x,         y,         1, h, rgb);
    gfx_fill_rect(x + w - 1, y,         1, h, rgb);
}

void gfx_draw_char(int x, int y, char ch, uint32_t rgb, int scale)
{
    const uint8_t *rows = font_glyph(ch);

    for (int row = 0; row < FONT_H; row++) {
        for (int col = 0; col < FONT_W; col++) {
            if (rows[row] & (1u << col))        /* bit 0 = leftmost pixel     */
                gfx_fill_rect(x + col * scale, y + row * scale, scale, scale, rgb);
        }
    }
}

void gfx_draw_string(int x, int y, const char *s, uint32_t rgb, int scale)
{
    for (; *s; s++, x += FONT_W * scale)
        gfx_draw_char(x, y, *s, rgb, scale);
}

int gfx_string_width(const char *s, int scale)
{
    return (int)strlen(s) * FONT_W * scale;
}

void gfx_present(void)
{
    if (!g.ready)
        return;

    for (int row = 0; row < g.h; row++) {
        const uint32_t *src = &backbuffer[row * g.w];
        uint32_t       *dst = (uint32_t *)(g.fb + (size_t)row * g.pitch);
        size_t          n   = (size_t)g.w;

        __asm__ volatile ("cld; rep movsl"
                          : "+S"(src), "+D"(dst), "+c"(n)
                          :
                          : "memory");
    }
}
