/* =============================================================================
 *  AGOS - Multiboot 1 information structure (multiboot.h)
 *  Layout per the Multiboot Specification 0.6.96. Only the fields AGOS uses
 *  are named; 64-bit values are split into lo/hi halves so the 32-bit kernel
 *  never needs 64-bit arithmetic.
 * ========================================================================== */
#ifndef AGOS_MULTIBOOT_H
#define AGOS_MULTIBOOT_H

#include <stddef.h>
#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

/* multiboot_info.flags */
#define MBI_FLAG_MEM       (1u << 0)    /* mem_lower / mem_upper valid        */
#define MBI_FLAG_MMAP      (1u << 6)    /* mmap_* valid                       */
#define MBI_FLAG_LOADER    (1u << 9)    /* boot_loader_name valid             */
#define MBI_FLAG_FRAMEBUF  (1u << 12)   /* framebuffer_* valid                */

#define MB_FB_TYPE_INDEXED  0
#define MB_FB_TYPE_RGB      1
#define MB_FB_TYPE_EGA_TEXT 2

#define MB_MMAP_AVAILABLE   1           /* usable RAM                         */

struct multiboot_info {
    uint32_t flags;                     /*   0 */
    uint32_t mem_lower;                 /*   4  KiB below 1 MiB               */
    uint32_t mem_upper;                 /*   8  KiB above 1 MiB               */
    uint32_t boot_device;               /*  12 */
    uint32_t cmdline;                   /*  16 */
    uint32_t mods_count;                /*  20 */
    uint32_t mods_addr;                 /*  24 */
    uint32_t syms[4];                   /*  28  a.out / ELF section info      */
    uint32_t mmap_length;               /*  44 */
    uint32_t mmap_addr;                 /*  48 */
    uint32_t drives_length;             /*  52 */
    uint32_t drives_addr;               /*  56 */
    uint32_t config_table;              /*  60 */
    uint32_t boot_loader_name;          /*  64  physical address of C string  */
    uint32_t apm_table;                 /*  68 */
    uint32_t vbe_control_info;          /*  72 */
    uint32_t vbe_mode_info;             /*  76 */
    uint16_t vbe_mode;                  /*  80 */
    uint16_t vbe_interface_seg;         /*  82 */
    uint16_t vbe_interface_off;         /*  84 */
    uint16_t vbe_interface_len;         /*  86 */
    uint32_t framebuffer_addr_lo;       /*  88  physical address, low half    */
    uint32_t framebuffer_addr_hi;       /*  92  high half (0 below 4 GiB)     */
    uint32_t framebuffer_pitch;         /*  96  bytes per scanline            */
    uint32_t framebuffer_width;         /* 100  pixels                        */
    uint32_t framebuffer_height;        /* 104  pixels                        */
    uint8_t  framebuffer_bpp;           /* 108 */
    uint8_t  framebuffer_type;          /* 109  MB_FB_TYPE_*                  */
    uint8_t  red_field_position;        /* 110  valid when type == RGB        */
    uint8_t  red_mask_size;             /* 111 */
    uint8_t  green_field_position;      /* 112 */
    uint8_t  green_mask_size;           /* 113 */
    uint8_t  blue_field_position;       /* 114 */
    uint8_t  blue_mask_size;            /* 115 */
} __attribute__((packed));

/* One memory-map entry. `size` does NOT include itself, so the next entry is
 * at (this + size + 4). */
struct multiboot_mmap_entry {
    uint32_t size;
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t len_lo;
    uint32_t len_hi;
    uint32_t type;
} __attribute__((packed));

_Static_assert(offsetof(struct multiboot_info, mmap_length) == 44,        "mbi layout");
_Static_assert(offsetof(struct multiboot_info, boot_loader_name) == 64,   "mbi layout");
_Static_assert(offsetof(struct multiboot_info, vbe_mode) == 80,           "mbi layout");
_Static_assert(offsetof(struct multiboot_info, framebuffer_addr_lo) == 88,"mbi layout");
_Static_assert(offsetof(struct multiboot_info, framebuffer_bpp) == 108,   "mbi layout");
_Static_assert(offsetof(struct multiboot_info, red_field_position) == 110,"mbi layout");
_Static_assert(sizeof(struct multiboot_mmap_entry) == 24,                 "mmap layout");

#endif /* AGOS_MULTIBOOT_H */
