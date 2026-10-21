/* =============================================================================
 *  AGOS - Kernel heap (mm/heap.c)
 * -----------------------------------------------------------------------------
 *  Memory layout of one chunk (a run of PMM pages):
 *
 *    [chunk header: 32 B][block][block]...[block][sentinel header: 16 B]
 *
 *  Every block starts with a 16-byte header holding its own size (bit 0 =
 *  in use) and the size of the physically preceding block, which makes both
 *  neighbours reachable in O(1) for coalescing. A zero-sized "used" sentinel
 *  ends each chunk. Free blocks additionally hold list links in their payload.
 * ========================================================================== */
#include "heap.h"
#include "lib.h"
#include "pmm.h"

#define HDR           16u
#define CHUNK_HDR     32u
#define MIN_BLOCK     32u               /* header + two list pointers, aligned */
#define MIN_CHUNK_PG  32u               /* 128 KiB                             */
#define HEAP_MAGIC    0xA605C0DEu

struct bhdr {
    uint32_t size;                      /* total block size; bit 0 = in use    */
    uint32_t prev;                      /* size of the preceding block, 0=first*/
    uint32_t magic;
    uint8_t  owner;
    uint8_t  pad[3];
};

struct fnode { struct fnode *next, *prev; };

struct chunk {
    struct chunk *next, *prev;
    uint32_t      pages;
    uint32_t      pa;                   /* physical address (for pmm_free)     */
    uint8_t       pad[CHUNK_HDR - 2 * sizeof(void *) - 8];
};

_Static_assert(sizeof(struct bhdr) == HDR, "block header must be 16 bytes");
_Static_assert(sizeof(struct chunk) == CHUNK_HDR, "chunk header must be 32 bytes");
_Static_assert(sizeof(struct fnode) <= MIN_BLOCK - HDR, "free node must fit a minimal block");

static struct chunk *chunks;
static struct fnode *freelist;
static uint32_t      nchunks;
static uint8_t       cur_owner;
static struct heap_stats stats;

static inline uint32_t blk_size(const struct bhdr *b) { return b->size & ~1u; }
static inline int      blk_used(const struct bhdr *b) { return (int)(b->size & 1u); }

static inline struct bhdr *blk_next(struct bhdr *b)
{
    return (struct bhdr *)((uint8_t *)b + blk_size(b));
}
static inline struct bhdr *blk_prev(struct bhdr *b)
{
    return (struct bhdr *)((uint8_t *)b - b->prev);
}
static inline struct fnode *node_of(struct bhdr *b) { return (struct fnode *)((uint8_t *)b + HDR); }
static inline struct bhdr  *blk_of(struct fnode *n) { return (struct bhdr *)((uint8_t *)n - HDR); }

static void free_insert(struct bhdr *b)
{
    struct fnode *n = node_of(b);

    n->prev = NULL;
    n->next = freelist;
    if (freelist)
        freelist->prev = n;
    freelist = n;
}

static void free_remove(struct bhdr *b)
{
    struct fnode *n = node_of(b);

    if (n->prev)
        n->prev->next = n->next;
    else
        freelist = n->next;
    if (n->next)
        n->next->prev = n->prev;
}

void heap_init(void)
{
    chunks = NULL;
    freelist = NULL;
    nchunks = 0;
    cur_owner = 0;
    memset(&stats, 0, sizeof(stats));
}

/* Add a chunk of at least `need` bytes of block space. Returns 0 on failure. */
static int heap_grow(uint32_t need)
{
    uint32_t pages = (need + CHUNK_HDR + HDR + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t pa;
    struct chunk *c;
    struct bhdr *b, *sentinel;
    uint32_t bsize;

    if (pages < MIN_CHUNK_PG)
        pages = MIN_CHUNK_PG;
    pa = pmm_alloc(pages);
    if (!pa) {                           /* memory is tight: retry minimal     */
        pages = (need + CHUNK_HDR + HDR + PAGE_SIZE - 1) / PAGE_SIZE;
        pa = pmm_alloc(pages);
        if (!pa)
            return 0;
    }

    c = phys_to_virt(pa);
    c->pages = pages;
    c->pa = pa;
    c->prev = NULL;
    c->next = chunks;
    if (chunks)
        chunks->prev = c;
    chunks = c;
    nchunks++;
    stats.chunk_bytes += pages * PAGE_SIZE;

    bsize = pages * PAGE_SIZE - CHUNK_HDR - HDR;
    b = (struct bhdr *)((uint8_t *)c + CHUNK_HDR);
    b->size = bsize;
    b->prev = 0;
    b->magic = HEAP_MAGIC;
    b->owner = 0;
    sentinel = (struct bhdr *)((uint8_t *)b + bsize);
    sentinel->size = 1;                  /* size 0, in use                     */
    sentinel->prev = bsize;
    sentinel->magic = HEAP_MAGIC;
    free_insert(b);
    return 1;
}

void *kmalloc(size_t size)
{
    uint32_t need;
    struct fnode *n;

    if (size > 0x7FFFFF00u)
        return NULL;
    size = (size + 15u) & ~(size_t)15u;
    if (size == 0)
        size = 16;
    need = (uint32_t)size + HDR;
    if (need < MIN_BLOCK)
        need = MIN_BLOCK;

    for (int attempt = 0; attempt < 2; attempt++) {
        for (n = freelist; n; n = n->next) {
            struct bhdr *b = blk_of(n);

            if (blk_size(b) < need)
                continue;

            free_remove(b);
            uint32_t rem = blk_size(b) - need;

            if (rem >= MIN_BLOCK) {      /* split off the tail                 */
                struct bhdr *t = (struct bhdr *)((uint8_t *)b + need);
                struct bhdr *after;

                b->size = need;
                t->size = rem;
                t->prev = need;
                t->magic = HEAP_MAGIC;
                t->owner = 0;
                after = blk_next(t);
                after->prev = rem;
                free_insert(t);
            }
            b->size |= 1u;
            b->magic = HEAP_MAGIC;
            b->owner = cur_owner < HEAP_MAX_OWNERS ? cur_owner : 0;

            uint32_t payload = blk_size(b) - HDR;
            stats.used_bytes += payload;
            if (stats.used_bytes > stats.peak_bytes)
                stats.peak_bytes = stats.used_bytes;
            stats.owner_bytes[b->owner] += payload;
            stats.allocs++;
            return (uint8_t *)b + HDR;
        }
        if (attempt == 0 && !heap_grow(need))
            return NULL;
    }
    return NULL;
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);

    if (p)
        memset(p, 0, size);
    return p;
}

void kfree(void *p)
{
    struct bhdr *b, *nx;
    uint32_t payload;

    if (!p)
        return;
    b = (struct bhdr *)((uint8_t *)p - HDR);
    if (b->magic != HEAP_MAGIC || !blk_used(b))
        return;                          /* double free / foreign pointer      */

    payload = blk_size(b) - HDR;
    stats.used_bytes -= payload;
    stats.owner_bytes[b->owner] -= payload;
    stats.allocs--;
    b->size &= ~1u;

    nx = blk_next(b);                    /* merge with the following block     */
    if (!blk_used(nx)) {
        free_remove(nx);
        b->size += blk_size(nx);
        nx = blk_next(b);
    }
    if (b->prev) {                       /* merge with the preceding block     */
        struct bhdr *pv = blk_prev(b);

        if (!blk_used(pv)) {
            free_remove(pv);
            pv->size += blk_size(b);
            b = pv;
            nx = blk_next(b);
        }
    }
    nx->prev = blk_size(b);

    /* A completely empty chunk goes back to the PMM (keep at least one). */
    if (b->prev == 0 && blk_size(nx) == 0 && nchunks > 1) {
        struct chunk *c = (struct chunk *)((uint8_t *)b - CHUNK_HDR);

        if (c->prev)
            c->prev->next = c->next;
        else
            chunks = c->next;
        if (c->next)
            c->next->prev = c->prev;
        nchunks--;
        stats.chunk_bytes -= c->pages * PAGE_SIZE;
        pmm_free(c->pa, c->pages);
        return;
    }
    free_insert(b);
}

void *krealloc(void *p, size_t size)
{
    struct bhdr *b;
    uint32_t old;
    void *q;

    if (!p)
        return kmalloc(size);
    if (size == 0) {
        kfree(p);
        return NULL;
    }
    b = (struct bhdr *)((uint8_t *)p - HDR);
    old = blk_size(b) - HDR;
    if (size <= old)
        return p;
    q = kmalloc(size);
    if (!q)
        return NULL;
    memcpy(q, p, old);
    kfree(p);
    return q;
}

char *kstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = kmalloc(n);

    if (d)
        memcpy(d, s, n);
    return d;
}

void    heap_set_owner(uint8_t owner) { cur_owner = owner; }
uint8_t heap_get_owner(void)          { return cur_owner; }

void heap_get_stats(struct heap_stats *out)
{
    *out = stats;
}
