/* =============================================================================
 *  AGOS - Physical memory manager (mm/pmm.h, mm/pmm.c)
 *  Bitmap allocator over 4 KiB frames, built from the Multiboot memory map.
 *  Paging is off, so physical == virtual; phys_to_virt() exists only so the
 *  host-side unit tests can run the same code against an arena.
 * ========================================================================== */
#ifndef AGOS_PMM_H
#define AGOS_PMM_H

#include <stdbool.h>
#include <stdint.h>
#include "multiboot.h"

#define PAGE_SIZE 4096u

/* Offset added to physical addresses to get a usable pointer. 0 in the
 * kernel; the unit tests point it into a malloc'ed arena. */
extern uintptr_t pmm_virt_offset;

static inline void *phys_to_virt(uint32_t pa)
{
    return (void *)((uintptr_t)pa + pmm_virt_offset);
}

/* Build the allocator.  kernel_end: first byte after the kernel image (incl.
 * .bss). Everything below it (and the first MiB) is never handed out, nor are
 * the Multiboot structures. Returns false if the memory map is unusable. */
bool pmm_init(const struct multiboot_info *mbi, uint32_t kernel_end);

/* Allocate `pages` physically contiguous frames; returns the physical address
 * or 0 on failure. The memory is NOT zeroed. */
uint32_t pmm_alloc(uint32_t pages);
void     pmm_free(uint32_t pa, uint32_t pages);

/* Statistics, in KiB. total = every usable byte in the memory map (below
 * 4 GiB); free = frames still available; kernel = image; meta = bitmap. */
struct pmm_stats {
    uint32_t total_kb;
    uint32_t free_kb;
    uint32_t kernel_kb;
    uint32_t meta_kb;
};
void pmm_get_stats(struct pmm_stats *out);

#endif /* AGOS_PMM_H */
