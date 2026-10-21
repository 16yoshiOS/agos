/* =============================================================================
 *  AGOS - S3 Vision 868 video driver (drivers/s3v.c)
 * -----------------------------------------------------------------------------
 *  Brings up a packed-pixel linear framebuffer on an S3 Vision 868-class chip
 *  (PCI 0x5333:0x8811) using only the legacy VGA register interface plus the
 *  S3 CRTC extensions:
 *
 *      Sequencer          index 0x3C4 / data 0x3C5
 *      Graphics ctrl      index 0x3CE / data 0x3CF
 *      Attribute ctrl     0x3C0 (flip-flop synced via Input Status #1, 0x3DA)
 *      CRT controller     index 0x3D4 / data 0x3D5   (CR00..CR1D + CR31..CR4A)
 *      PCI config space   0xCF8 / 0xCFC (through drivers/pci.c)
 *
 *  The mode is programmed "the XFree86 s3 driver way": extended modeline bit
 *  (CR32[4]), high-res count enable (CR36[1]), offset register in 8-byte
 *  units (CR19 + CR3B extension), and the LFB enabled through the chipset
 *  control MMIO window at BAR+0x4040 (S3_GEN_MODE, bit 2 = "linear addressing
 *  mode"). When that window is not addressable we report failure so the
 *  caller can fall back to GRUB's framebuffer or VGA text.
 *
 *  Write-combining: agos boots without paging and without touching the MTRRs
 *  yet; rep movsd into 0xA0000 still works (uncached WC would only speed it
 *  up). A future CPU bring-up module should mark [0xA0000, +VRAM) as WC via
 *  WRMSR(0x2FF..) before enabling paging.
 * ========================================================================== */
#include "s3v.h"

#include "io.h"
#include "lib.h"
#include "pci.h"

#define S3_VENDOR_ID         0x5333u        /* ASCII "S3"                     */
#define S3_VISION868_DEVICE  0x8811u

/* Chipset-control register offsets inside the card's MMIO BAR (vision.cfg /
 * XFree86 xf86-video-s3 use the same map). */
#define S3_GEN_MODE_OFF      0x4040u
#define  S3_GEN_LFB_ENABLE   0x00000004u    /* linear addressing mode         */
#define S3_BIU_MISC_OFF      0x4844u
#define  S3_BIU_MAP_1M       0x00000007u    /* Axxxxx-Bxxxxx decoded to VRAM  */

/* --- Port helpers ----------------------------------------------------------- */
static inline void seq_write(uint8_t reg, uint8_t val)
{
    outb(0x3C4, reg);
    outb(0x3C5, val);
}

static inline void gc_write(uint8_t reg, uint8_t val)
{
    outb(0x3CE, reg);
    outb(0x3CF, val);
}

static inline void crtc_write(uint8_t reg, uint8_t val)
{
    outb(0x3D4, reg);
    outb(0x3D5, val);
}

/* The attribute controller shares its port with a read-only palette latch;
 * writing requires resetting the index/data flip-flop first by reading
 * Input Status #1 (0x3DA). Setting bit 5 of the index keeps the display on. */
static void attr_write(uint8_t reg, uint8_t val)
{
    (void)inb(0x3DA);
    outb(0x3C0, (uint8_t)(reg | 0x20));
    outb(0x3C0, val);
    (void)inb(0x3DA);
}

/* --- Mode table ---------------------------------------------------------------
 * Timing values are already divided by 8 (the CRTC counts characters).
 * Only 640x480 is guaranteed to fit every Vision 868 board in RGB24
 * (900 KiB of VRAM); the bigger modes pass the sized-LFB gate first. */
struct s3_mode {
    int w, h;
    uint8_t h_total, h_display, h_sync_start, h_sync_end, h_blank_end;
    uint8_t v_total, v_display, v_sync_start, v_sync_end, v_blank_end;
    uint8_t cr17;                           /* sync polarities                */
};

static const struct s3_mode modes[] = {
    /*  640x480 @ ~60 Hz : H 800/96/148/165  V 525/10/35/45 (dots/lines) */
    {  640, 480, 100, 80, 84,  90, 106,  64, 49, 60, 61,  72, 0xA3 },
    /*  800x600 @ ~56 Hz : H 896/112/232/256 V 625/1/64/66                */
    {  800, 600, 112, 100, 116, 120, 144,  79, 75, 80, 81, 106, 0x83 },
    /* 1024x768 @ ~60 Hz : H 1088/128/344/376 V 806/3/80/81               */
    { 1024, 768, 136, 128, 144, 152, 168, 101, 96, 101, 105, 131, 0xE3 },
};

static struct {
    bool     ready;
    void    *lfb;
    uint8_t *mmio;                          /* chipset-control window (or 0)  */
    uint32_t pitch;
    int      bpp;
    int      w, h;
} sv;

bool     s3v_ready(void)  { return sv.ready; }
void    *s3v_lfb(void)    { return sv.lfb; }
int      s3v_width(void)  { return sv.w; }
int      s3v_height(void) { return sv.h; }
uint32_t s3v_pitch(void)  { return sv.pitch; }
int      s3v_bpp(void)    { return sv.bpp; }

/* --- CRTC programming --------------------------------------------------------- */
static void s3_program_crtc(const struct s3_mode *m, int bytespp)
{
    const uint32_t hd    = m->h_display - 1u;      /* CR01 value              */
    const uint32_t vd    = m->v_display - 1u;      /* CR06 value              */
    const uint32_t ht_m5 = (uint32_t)m->h_total - 5u;
    const uint32_t vt_m2 = (uint32_t)m->v_total - 2u;
    const uint32_t offset = (uint32_t)m->w * (uint32_t)bytespp / 8u; /* CR19 unit */

    /* Sequencer: full-plane writes, no shift/chain-4, screen on. */
    seq_write(0x00, 0x03);                  /* soft reset both sides          */
    seq_write(0x01, 0x00);                  /* synchronous clocking           */
    seq_write(0x02, 0x0F);                  /* write all planes               */
    seq_write(0x03, 0x00);                  /* default XOR, no mask           */
    seq_write(0x04, 0x00);                  /* no odd/even split              */

    /* Graphics controller: CPU access straight to the plane memory. */
    gc_write(0x00, 0x00);
    gc_write(0x01, 0x00);
    gc_write(0x02, 0x00);
    gc_write(0x03, 0x00);
    gc_write(0x04, 0x00);
    gc_write(0x05, 0x00);
    gc_write(0x06, 0x04);                   /* even/odd reset, serial delay off*/
    gc_write(0x07, 0x05);                   /* colour compare, don't care     */

    /* Attribute controller: video + palette path on for packed modes. */
    attr_write(0x10, 0x41);
    attr_write(0x11, 0x00);                 /* no colour switching            */
    attr_write(0x12, 0x0F);                 /* plane enable all               */
    attr_write(0x13, 0x08);                 /* pixel pan = normal             */
    attr_write(0x14, 0x00);                 /* colour page = 0                */
    (void)inb(0x3DA);                       /* leave index mode               */

    /* Standard CRTC timing registers (0x3D4/0x3D5). */
    crtc_write(0x00, (uint8_t)(ht_m5 & 0xFF));
    crtc_write(0x01, (uint8_t)(hd & 0xFF));
    crtc_write(0x02, m->h_sync_start);
    crtc_write(0x03, (uint8_t)(((m->h_sync_end & 0x0Fu) << 3) |
                                ((~m->h_sync_end & 0x10u) >> 1) | 0x80));
    crtc_write(0x04, (uint8_t)(m->h_blank_end & 0xFF));
    crtc_write(0x05, (uint8_t)(((m->h_blank_end & 0x10u) << 1) |
                               ((m->h_sync_end & 0x10u) >> 1) |
                                (hd & 0x1Fu)));
    crtc_write(0x06, (uint8_t)(vd & 0xFF));
    crtc_write(0x07, (uint8_t)(((vd & 0x100u) >> 4) |          /* bit5 */
                                (((vd & 0x200u) != 0) << 3) |   /* bit3 */
                                ((m->v_sync_start & 0x08u) << 1) |
                                 (vt_m2 & 0x1Fu)));              /* bits4-0    */
    crtc_write(0x08, 0x00);                 /* row scan: 1 line per row       */
    crtc_write(0x09, (uint8_t)(m->v_sync_end & 0x1Fu));
    crtc_write(0x0A, 0x0E);                 /* cursor off (max scan line 14)  */
    crtc_write(0x0B, (uint8_t)((((vd & 0x100u) >> 6) |
                                ((vt_m2 & 0x20u) << 2) |
                                 (vt_m2 & 0x1Fu))));
    crtc_write(0x0C, 0x00);                 /* start address = 0              */
    crtc_write(0x0D, 0x00);
    crtc_write(0x0E, 0x00);
    crtc_write(0x0F, 0x00);
    crtc_write(0x10, m->v_sync_start);
    crtc_write(0x11, (uint8_t)(m->v_blank_end & 0xFF));
    crtc_write(0x12, (uint8_t)(vt_m2 & 0xFF));
    crtc_write(0x13, (uint8_t)(offset & 0xFF));       /* presync/interlace    */
    crtc_write(0x17, m->cr17);                        /* sync polarities      */
    crtc_write(0x19, (uint8_t)(offset & 0xFF));       /* CR19: offset (bytes/8)*/
    crtc_write(0x1A, (uint8_t)m->w);                  /* underline length     */
    crtc_write(0x1B, (uint8_t)((offset >> 8) | 0x20u));/* ext. offset bits     */
    crtc_write(0x1C, 0x02);                            /* byte panning         */
    crtc_write(0x1D, 0x00);                            /* pixel panning        */

    /* S3 extension block. Writing CR31 unlocks indices >= 0x31. */
    crtc_write(0x31, 0x86);                 /* EM1: ext. CRTC regs unlocked   */
    crtc_write(0x32, 0x14);                 /* EM2: extended modeline on      */
    crtc_write(0x33, 0x00);                 /* no interlace                   */
    crtc_write(0x34, (uint8_t)((((ht_m5 & 0x100u) >> 4) |
                                ((hd & 0x100u) >> 6))));
    crtc_write(0x35, 0x40);                 /* Gen Mode Control               */
    crtc_write(0x36, 0x92);                 /* Config Ctrl 1: high-res count  */
    crtc_write(0x37, 0x01);                 /* Config Ctrl 2                  */
    crtc_write(0x38, 0x54);                 /* Misc: fast VID path            */
    crtc_write(0x39, 0x00);                 /* Max Color                      */
    crtc_write(0x3A, 0x00);                 /* Screen 1                       */
    crtc_write(0x3B, (uint8_t)((offset >> 8) & 0x0Fu)); /* Offscreen Value    */
    crtc_write(0x40, 0x30);                 /* Ext. Interrupt Enable          */
    crtc_write(0x41, 0x00);                 /* System I/O Control             */
    crtc_write(0x42, 0xC4);                 /* LCD/Misc                       */
    crtc_write(0x45, 0x10);                 /* Power Mgmt: no doze/standby    */
    crtc_write(0x4A, 0x0A);                 /* Ext Func: 8 MiB aperture base  */

    /* Keep the DAC/palette path alive and turn the screen back on. */
    seq_write(0x00, 0x01);
}

/* Map the chipset MMIO window and switch on linear framebuffer addressing.
 * Returns false if the BAR is missing, is an I/O BAR, or is too small. */
static bool s3_enable_lfb(const struct pci_dev *d, unsigned bar_index)
{
    uint32_t size = pci_bar_size(d, bar_index);
    uint32_t raw  = d->bar[bar_index];

    if ((raw & 1u) || size < 0x5000u)       /* must be memory, cover 0x48xx   */
        return false;

    /* Reprogram the BAR to a fixed probe-friendly base. Paging is OFF in
     * agos, so physical == virtual and the window is directly dereferencable. */
    const uint32_t base = 0xF0000000u;      /* classic PCI aperture slot      */

    /* const_cast: we update our cached copy of the BAR after reprogramming;
     * drivers/pci.c keeps no other alias of this struct. */
    struct pci_dev *mut = (struct pci_dev *)d;

    pci_write32(mut->bus, mut->dev, mut->fn,
                (uint8_t)(PCI_REG_BAR0 + bar_index * 4), base | (raw & 0x7u));
    mut->bar[bar_index] = base | (raw & 0x7u);
    pci_set_command(mut, PCI_CMD_MEMORY | PCI_CMD_MASTER);

    sv.mmio = (uint8_t *)(uintptr_t)base;

    volatile uint32_t *gen = (volatile uint32_t *)(sv.mmio + S3_GEN_MODE_OFF);
    volatile uint32_t *biu = (volatile uint32_t *)(sv.mmio + S3_BIU_MISC_OFF);

    /* BIU Misc: point the Axxxxx hole at VRAM bank 0 (1 MiB decoding window
     * is plenty for our <= 2.25 MiB modes because the LFB spans everything). */
    *biu = (*biu & ~0x7u) | S3_BIU_MAP_1M;

    /* S3_GEN_MODE bit 2 turns the CPU -> VRAM linear path on. */
    *gen |= S3_GEN_LFB_ENABLE;

    /* Readback sanity check: emulations/chips that ignore the write leave
     * the bit clear — better to fail now than paint into a black screen. */
    return (*gen & S3_GEN_LFB_ENABLE) != 0;
}

/* Size of the LFB window (== VRAM aperture) advertised by the BARs. */
static uint32_t s3_vram_window(const struct pci_dev *d)
{
    uint32_t s1 = pci_bar_size(d, 1);
    uint32_t s0 = pci_bar_size(d, 0);

    return s1 > s0 ? s1 : s0;
}

/* Where does this card put its VRAM for CPU access? Two sources, in order of
 * trustworthiness: the PCI BAR0 aperture base (Vision-era cards mirror the
 * whole framebuffer there, typically at 0xFD800000 or similar), and the CRTC
 * start-address registers (CR0E/CR0F hold bits 22:16 of the display start,
 * which is VRAM offset 0 when programmed as we do). We use BAR0 when it is a
 * sane memory window, otherwise fall back to the legacy A0000 hole. Paging is
 * OFF in agos, so both are plain physical == virtual pointers. */
static void *s3_pick_lfb_base(const struct pci_dev *d)
{
    uint32_t raw0 = d->bar[0];

    if (!(raw0 & 1u)) {                     /* memory BAR                     */
        uint32_t base = raw0 & 0xFFFFFFF0u;

        if (base >= 0x00100000u && base <= 0xFE000000u)
            return (void *)(uintptr_t)base; /* real aperture, above 1 MiB     */
    }
    return (void *)(uintptr_t)0xA0000;      /* legacy VGA hole fallback       */
}

/* Try one (mode, bpp) combination end-to-end. */
static bool s3_try_mode(const struct pci_dev *d, const struct s3_mode *m, int bpp)
{
    const uint32_t need = (uint32_t)m->w * (uint32_t)m->h * (uint32_t)(bpp / 8);

    if (s3_vram_window(d) < need)
        return false;                       /* VRAM cannot hold the mode       */

    if (!s3_enable_lfb(d, 1) && !s3_enable_lfb(d, 0))
        return false;

    s3_program_crtc(m, bpp / 8);

    sv.lfb   = s3_pick_lfb_base(d);
    sv.pitch = (uint32_t)m->w * (uint32_t)(bpp / 8);
    sv.bpp   = bpp;
    sv.w     = m->w;
    sv.h     = m->h;
    sv.ready = true;
    return true;
}

bool s3v_init(int w, int h, int bpp)
{
    struct pci_dev d;
    const struct s3_mode *m = NULL;

    sv.ready = false;
    if (bpp != 24 && bpp != 32)
        return false;

    if (!pci_find(S3_VENDOR_ID, S3_VISION868_DEVICE, &d))
        return false;

    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
        if (modes[i].w == w && modes[i].h == h) {
            m = &modes[i];
            break;
        }
    if (!m)
        return false;

    if (s3_try_mode(&d, m, bpp))
        return true;

    /* Fallback: the mode the entire Vision family can always do. */
    return s3_try_mode(&d, &modes[0], 24);
}

/* --- Back buffer -> VRAM ------------------------------------------------------ */
void s3v_copy_rows(void *dst, const uint32_t *src, int w, int rows,
                   uint32_t dst_pitch)
{
    uint8_t *d = (uint8_t *)dst;

    if (!d || !src || w <= 0 || rows <= 0)
        return;

    if (sv.bpp == 32) {
        /* Straight dword stream: REP MOVSD beats any C loop on a 486+. */
        while (rows--) {
            size_t n = (size_t)w;

            __asm__ volatile ("cld; rep movsl"
                              : "+S"(src), "+D"(d), "+c"(n)
                              :
                              : "memory");
            d += dst_pitch - (size_t)w * 4u;
        }
        return;
    }

    /* RGB24: pack XRGB -> B,G,R triples. One pass, no temp buffer. */
    while (rows--) {
        for (int x = 0; x < w; x++) {
            const uint32_t p = src[x];

            d[3 * x + 0] = (uint8_t)p;          /* B */
            d[3 * x + 1] = (uint8_t)(p >> 8);   /* G */
            d[3 * x + 2] = (uint8_t)(p >> 16);  /* R */
        }
        src += w;
        d   += dst_pitch;
    }
}

