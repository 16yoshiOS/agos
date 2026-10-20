/* =============================================================================
 *  AGOS - Global Descriptor Table + TSS (gdt.c)
 * -----------------------------------------------------------------------------
 *  Flat memory model: every code/data segment covers the full 4 GiB starting
 *  at 0, so segmentation is effectively a no-op and only privilege levels
 *  matter. Layout:
 *      0x00 null   0x08 kernel code   0x10 kernel data
 *      0x18 user code (DPL 3)         0x20 user data (DPL 3)
 *      0x28 TSS
 * ========================================================================== */
#include "gdt.h"
#include "lib.h"

extern void gdt_flush(const struct gdt_ptr *gp);    /* interrupt_stubs.asm */
extern void tss_flush(void);
extern uint8_t kernel_stack_top[];                  /* boot/start.asm      */

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gdt_pointer;
static struct tss_entry tss;

void gdt_encode(struct gdt_entry *e, uint32_t base, uint32_t limit,
                uint8_t access, uint8_t gran)
{
    e->base_low    = (uint16_t)(base & 0xFFFF);
    e->base_mid    = (uint8_t)((base >> 16) & 0xFF);
    e->base_high   = (uint8_t)((base >> 24) & 0xFF);
    e->limit_low   = (uint16_t)(limit & 0xFFFF);
    e->granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    e->access      = access;
}

void gdt_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

void gdt_init(void)
{
    memset(&tss, 0, sizeof(tss));
    tss.ss0        = GDT_KERNEL_DATA;
    tss.esp0       = (uint32_t)(uintptr_t)kernel_stack_top;
    tss.iomap_base = sizeof(tss);       /* offset past the TSS = no I/O bitmap */

    gdt_encode(&gdt[0], 0, 0, 0, 0);                                    /* null */
    gdt_encode(&gdt[1], 0, 0xFFFFF, GDT_ACCESS_KCODE, GDT_GRAN_4K_32);
    gdt_encode(&gdt[2], 0, 0xFFFFF, GDT_ACCESS_KDATA, GDT_GRAN_4K_32);
    gdt_encode(&gdt[3], 0, 0xFFFFF, GDT_ACCESS_UCODE, GDT_GRAN_4K_32);
    gdt_encode(&gdt[4], 0, 0xFFFFF, GDT_ACCESS_UDATA, GDT_GRAN_4K_32);
    gdt_encode(&gdt[5], (uint32_t)(uintptr_t)&tss, sizeof(tss) - 1,
               GDT_ACCESS_TSS, GDT_GRAN_BYTE);

    gdt_pointer.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_pointer.base  = (uint32_t)(uintptr_t)gdt;

    gdt_flush(&gdt_pointer);
    tss_flush();
}
