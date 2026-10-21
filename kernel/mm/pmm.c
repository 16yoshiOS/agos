/* =============================================================================
 *  AGOS - Physical memory manager (mm/pmm.c)
 * -----------------------------------------------------------------------------
 *  Frame bitmap: bit set = in use. The bitmap starts "everything used"; only
 *  frames that lie fully inside an AVAILABLE memory-map region above 1 MiB are
 *  released, and then the kernel image, the bitmap itself and the Multiboot
 *  data are claimed again. The first MiB (BIOS, VGA, option ROMs, GRUB's own
 *  data) is therefore never allocated.
 * ========================================================================== */
#include "pmm.h"
#include "lib.h"

#define LOW_MEM_LIMIT 0x100000u         /* never allocate below 1 MiB          */
#define MAX_FRAMES    0x100000u         /* 4 GiB / 4 KiB                       */

uintptr_t pmm_virt_offset;

static uint8_t  *bitmap;
static uint32_t  nframes;
static uint32_t  free_frames;
static uint32_t  hint;                  /* first frame worth searching from    */
static uint32_t  total_kb, kernel_kb, meta_kb;

struct region { uint32_t start, end; }; /* [start, end), clipped below 4 GiB   */

static inline int  bm_get(uint32_t f) { return (bitmap[f >> 3] >> (f & 7)) & 1; }
static inline void bm_set(uint32_t f) { bitmap[f >> 3] |= (uint8_t)(1u << (f & 7)); }
static inline void bm_clr(uint32_t f) { bitmap[f >> 3] &= (uint8_t)~(1u << (f & 7)); }

static uint32_t align_up(uint32_t v)   { return (v + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }
static uint32_t align_down(uint32_t v) { return v & ~(PAGE_SIZE - 1); }

/* Fetch usable region `idx` (0-based) from the memory map; falls back to the
 * old mem_lower/mem_upper fields. Returns false when there are no more. */
static bool get_region(const struct multiboot_info *mbi, unsigned idx, struct region *r)
{
    if (mbi->flags & MBI_FLAG_MMAP) {
        uint32_t p = mbi->mmap_addr, end = mbi->mmap_addr + mbi->mmap_length;
        unsigned seen = 0;

        while (p + sizeof(struct multiboot_mmap_entry) <= end) {
            const struct multiboot_mmap_entry *e = phys_to_virt(p);

            if (e->type == MB_MMAP_AVAILABLE && e->addr_hi == 0) {
                if (seen++ == idx) {
                    uint32_t s = e->addr_lo;
                    uint32_t len = e->len_lo;
                    uint32_t e32 = (e->len_hi || s + len < s) ? 0xFFFFF000u : s + len;

                    r->start = s;
                    r->end   = e32;
                    return true;
                }
            }
            p += e->size + 4;
        }
        return false;
    }

    if (mbi->flags & MBI_FLAG_MEM) {
        if (idx == 0) {
            r->start = 0;
            r->end   = mbi->mem_lower * 1024u;
            return true;
        }
        if (idx == 1) {
            r->start = LOW_MEM_LIMIT;
            r->end   = LOW_MEM_LIMIT + mbi->mem_upper * 1024u;
            return true;
        }
    }
    return false;
}

static void claim_range(uint32_t start, uint32_t end)      /* mark used */
{
    if (end <= start)
        return;
    for (uint32_t f = align_down(start) / PAGE_SIZE; f < align_up(end) / PAGE_SIZE && f < nframes; f++) {
        if (!bm_get(f)) {
            bm_set(f);
            free_frames--;
        }
    }
}

static void release_range(uint32_t start, uint32_t end)    /* mark free */
{
    if (start < LOW_MEM_LIMIT)
        start = LOW_MEM_LIMIT;
    for (uint32_t f = align_up(start) / PAGE_SIZE; f < align_down(end) / PAGE_SIZE && f < nframes; f++) {
        if (bm_get(f)) {
            bm_clr(f);
            free_frames++;
        }
    }
}

bool pmm_init(const struct multiboot_info *mbi, uint32_t kernel_end)
{
    struct region r;
    uint32_t top = 0, kstart = LOW_MEM_LIMIT;

    total_kb = 0;
    for (unsigned i = 0; get_region(mbi, i, &r); i++) {
        if (r.end > top)
            top = r.end;
        total_kb += (r.end - r.start) >> 10;     /* sums stay far below 4 TiB  */
    }
    if (top <= LOW_MEM_LIMIT)
        return false;

    nframes = align_down(top) / PAGE_SIZE;
    if (nframes > MAX_FRAMES)
        nframes = MAX_FRAMES;

    uint32_t bm_bytes = (nframes + 7) / 8;
    uint32_t bm_pa    = 0;

    /* Place the bitmap right after the kernel if that spot is usable RAM,
     * otherwise in the first region that is big enough. */
    for (unsigned i = 0; get_region(mbi, i, &r); i++) {
        uint32_t lo = align_up(kernel_end > r.start ? kernel_end : r.start);

        if (lo < LOW_MEM_LIMIT)
            lo = LOW_MEM_LIMIT;
        if (lo + bm_bytes <= r.end && lo >= r.start) {
            bm_pa = lo;
            break;
        }
    }
    if (!bm_pa)
        return false;

    bitmap = phys_to_virt(bm_pa);
    memset(bitmap, 0xFF, bm_bytes);              /* everything used            */
    /* Bits past nframes in the last byte stay set (= unavailable). */
    free_frames = 0;
    hint = LOW_MEM_LIMIT / PAGE_SIZE;

    for (unsigned i = 0; get_region(mbi, i, &r); i++)
        release_range(r.start, r.end);

    /* Claim what must not be handed out. */
    claim_range(kstart, kernel_end);
    claim_range(bm_pa, bm_pa + bm_bytes);
    claim_range((uint32_t)(uintptr_t)mbi - (uint32_t)pmm_virt_offset,
                (uint32_t)(uintptr_t)mbi - (uint32_t)pmm_virt_offset + sizeof(*mbi));
    if (mbi->flags & MBI_FLAG_MMAP)
        claim_range(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);
    if (mbi->flags & MBI_FLAG_LOADER) {
        const char *s = phys_to_virt(mbi->boot_loader_name);
        claim_range(mbi->boot_loader_name, mbi->boot_loader_name + (uint32_t)strlen(s) + 1);
    }
    if (mbi->flags & (1u << 2)) {                /* cmdline valid              */
        const char *s = phys_to_virt(mbi->cmdline);
        claim_range(mbi->cmdline, mbi->cmdline + (uint32_t)strlen(s) + 1);
    }
    if (mbi->flags & (1u << 3)) {                /* modules                    */
        const uint32_t *m = phys_to_virt(mbi->mods_addr);

        claim_range(mbi->mods_addr, mbi->mods_addr + mbi->mods_count * 16u);
        for (uint32_t i = 0; i < mbi->mods_count; i++)
            claim_range(m[i * 4], m[i * 4 + 1]);
    }

    kernel_kb = (kernel_end - kstart) >> 10;
    meta_kb   = align_up(bm_bytes) >> 10;
    return true;
}

uint32_t pmm_alloc(uint32_t pages)
{
    if (!bitmap || pages == 0 || pages > free_frames)
        return 0;

    for (int pass = 0; pass < 2; pass++) {
        uint32_t f     = pass == 0 ? hint : LOW_MEM_LIMIT / PAGE_SIZE;
        uint32_t limit = pass == 0 ? nframes : hint + pages;

        if (limit > nframes)
            limit = nframes;
        while (f + pages <= limit) {
            uint32_t run = 0;

            if ((f & 7) == 0 && bitmap[f >> 3] == 0xFF) {   /* skip 8 used  */
                f += 8;
                continue;
            }
            while (run < pages && !bm_get(f + run))
                run++;
            if (run == pages) {
                for (uint32_t i = 0; i < pages; i++)
                    bm_set(f + i);
                free_frames -= pages;
                if (pages == 1 || f == hint)
                    hint = f + pages;
                return f * PAGE_SIZE;
            }
            f += run + 1;                        /* skip past the used frame   */
        }
    }
    return 0;
}

void pmm_free(uint32_t pa, uint32_t pages)
{
    uint32_t f = pa / PAGE_SIZE;

    if (!bitmap || (pa & (PAGE_SIZE - 1)) || f < LOW_MEM_LIMIT / PAGE_SIZE ||
        f + pages > nframes)
        return;
    for (uint32_t i = 0; i < pages; i++) {
        if (bm_get(f + i)) {
            bm_clr(f + i);
            free_frames++;
        }
    }
    if (f < hint)
        hint = f;
}

void pmm_get_stats(struct pmm_stats *out)
{
    out->total_kb  = total_kb;
    out->free_kb   = free_frames * (PAGE_SIZE / 1024);
    out->kernel_kb = kernel_kb;
    out->meta_kb   = meta_kb;
}
