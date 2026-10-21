/* =============================================================================
 *  AGOS - Kernel linker script (linker.ld)
 * -----------------------------------------------------------------------------
 *  Output is a 32-bit ELF that GRUB loads with the Multiboot protocol. The
 *  kernel is placed at 1 MiB (0x100000), the conventional start of extended
 *  memory; everything below is BIOS/VGA territory.
 *
 *  Layout (each segment page-aligned so GRUB can load them independently):
 *     0x00100000  .multiboot   Multiboot header - must be in the first 8 KiB
 *                 .text
 *                 .rodata
 *                 .data
 *                 .bss         (incl. kernel stack and the graphics back
 *                               buffer; zero-filled by the loader from the
 *                               ELF memory size, takes no space in the ISO)
 *                 kernel_end
 * ========================================================================== */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

KERNEL_BASE = 0x100000;

PHDRS
{
    text   PT_LOAD FLAGS(5);            /* R + X  */
    rodata PT_LOAD FLAGS(4);            /* R      */
    data   PT_LOAD FLAGS(6);            /* R + W  */
}

SECTIONS
{
    . = KERNEL_BASE;
    kernel_start = .;

    .text : ALIGN(4K)
    {
        KEEP(*(.multiboot))             /* header first, inside the first 8 KiB */
        *(.text .text.*)
    } :text

    .rodata : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    } :rodata

    .data : ALIGN(4K)
    {
        *(.data .data.*)
    } :data

    .bss : ALIGN(4K)
    {
        kernel_bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(4K);
        kernel_bss_end = .;
    } :data

    kernel_end = .;

    /* Build-time sanity. The "Multiboot header within the first 8 KiB of the
     * file" rule is verified on the finished ELF by tools/check_kernel.py. */
    ASSERT(kernel_end > KERNEL_BASE, "empty kernel image")

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}
