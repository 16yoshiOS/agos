/* =============================================================================
 *  AGOS - Kernel heap (mm/heap.h, mm/heap.c)
 *  General-purpose allocator on top of the physical memory manager.
 *  - First-fit free list with boundary tags and immediate coalescing.
 *  - Grows in chunks (>= 128 KiB) taken from the PMM; fully free chunks are
 *    handed back, so closing an application really returns its RAM.
 *  - Every block records an "owner" id (the task that was running when it was
 *    allocated), which is how the Task Manager shows memory per application.
 *  Not interrupt-safe: never call from an IRQ handler.
 * ========================================================================== */
#ifndef AGOS_HEAP_H
#define AGOS_HEAP_H

#include <stddef.h>
#include <stdint.h>

#define HEAP_MAX_OWNERS 16

void  heap_init(void);

void *kmalloc(size_t size);             /* 16-byte aligned, NULL on failure   */
void *kzalloc(size_t size);             /* zero-filled                        */
void *krealloc(void *p, size_t size);   /* p may be NULL                      */
void  kfree(void *p);                   /* p may be NULL                      */
char *kstrdup(const char *s);

/* Owner accounting. The owner is a small integer (see sys/acct.h). */
void     heap_set_owner(uint8_t owner);
uint8_t  heap_get_owner(void);

struct heap_stats {
    uint32_t used_bytes;                /* live payload bytes                  */
    uint32_t peak_bytes;
    uint32_t chunk_bytes;               /* RAM the heap holds from the PMM     */
    uint32_t allocs;                    /* live allocations                    */
    uint32_t owner_bytes[HEAP_MAX_OWNERS];
};
void heap_get_stats(struct heap_stats *out);

#endif /* AGOS_HEAP_H */
