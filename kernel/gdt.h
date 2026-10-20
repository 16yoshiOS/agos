/* =============================================================================
 *  AGOS - Global Descriptor Table + Task State Segment (gdt.h / gdt.c)
 * ========================================================================== */
#ifndef AGOS_GDT_H
#define AGOS_GDT_H

#include <stdint.h>

/* Segment selectors (index << 3 | RPL). Must match interrupt_stubs.asm. */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x1B            /* 0x18 | RPL 3 */
#define GDT_USER_DATA   0x23            /* 0x20 | RPL 3 */
#define GDT_TSS         0x28

#define GDT_ENTRIES     6

/* Access bytes: P | DPL | S | type */
#define GDT_ACCESS_KCODE 0x9A           /* present, ring 0, code, exec/read   */
#define GDT_ACCESS_KDATA 0x92           /* present, ring 0, data, read/write  */
#define GDT_ACCESS_UCODE 0xFA           /* present, ring 3, code, exec/read   */
#define GDT_ACCESS_UDATA 0xF2           /* present, ring 3, data, read/write  */
#define GDT_ACCESS_TSS   0x89           /* present, 32-bit TSS (available)    */

/* Granularity nibble: G (4 KiB units) | D/B (32-bit) -> 0xC0 */
#define GDT_GRAN_4K_32   0xC0
#define GDT_GRAN_BYTE    0x00

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;               /* high nibble flags | limit[19:16]   */
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* 32-bit Task State Segment. Only ss0/esp0 matter for a flat-model kernel:
 * they give the CPU the stack to switch to on a ring 3 -> ring 0 transition. */
struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct gdt_entry) == 8,   "GDT entry must be 8 bytes");
_Static_assert(sizeof(struct gdt_ptr)   == 6,   "GDTR image must be 6 bytes");
_Static_assert(sizeof(struct tss_entry) == 104, "32-bit TSS must be 104 bytes");

/* Fill one descriptor. Pure function (no hardware access). */
void gdt_encode(struct gdt_entry *e, uint32_t base, uint32_t limit,
                uint8_t access, uint8_t gran);

/* Build and load the AGOS GDT + TSS; reload all segment registers. */
void gdt_init(void);

/* Set the stack the CPU switches to when entering ring 0 from ring 3. */
void gdt_set_kernel_stack(uint32_t esp0);

#endif /* AGOS_GDT_H */
