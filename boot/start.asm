; =============================================================================
;  AGOS - Multiboot entry stub (boot/start.asm)
; -----------------------------------------------------------------------------
;  GRUB (or QEMU's -kernel) loads kernel.elf at 1 MiB, switches the CPU into
;  32-bit protected mode with a flat GDT, and jumps to _start with:
;      EAX = 0x2BADB002          Multiboot magic
;      EBX = physical address of the multiboot_info structure
;      interrupts disabled, paging disabled, A20 enabled
;  GRUB's own GDT is NOT ours to keep: kernel_main() installs the AGOS GDT.
;
;  This file provides the Multiboot 1 header, a kernel stack, and the call into
;  C. The header requests: page-aligned modules, a memory map, and a linear
;  1024x768x32 framebuffer (GRUB falls back to the nearest mode it can set).
; =============================================================================

BITS 32

MB_MAGIC     equ 0x1BADB002
MB_ALIGN     equ 1 << 0                 ; align loaded modules on page borders
MB_MEMINFO   equ 1 << 1                 ; provide memory map
MB_VIDEO     equ 1 << 2                 ; provide video mode info / set mode
MB_FLAGS     equ MB_ALIGN | MB_MEMINFO | MB_VIDEO
MB_CHECKSUM  equ -(MB_MAGIC + MB_FLAGS) ; magic + flags + checksum == 0 (mod 2^32)

STACK_SIZE   equ 16384                  ; 16 KiB boot/kernel stack

; --- Multiboot header --------------------------------------------------------
; Must lie within the first 8 KiB of the file and be 4-byte aligned; the linker
; script puts this section first.
section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    ; Address fields: only used when flag bit 16 is set (a.out kludge). We are
    ; an ELF, so they are zero, but they must be present because the video
    ; fields below sit at fixed offsets 32..47 of the header.
    dd 0                                ; header_addr
    dd 0                                ; load_addr
    dd 0                                ; load_end_addr
    dd 0                                ; bss_end_addr
    dd 0                                ; entry_addr
    ; Video request
    dd 0                                ; mode_type: 0 = linear framebuffer
    dd 1024                             ; width
    dd 768                              ; height
    dd 32                               ; depth (bits per pixel)

; --- Kernel stack ------------------------------------------------------------
section .bss
align 16
stack_bottom:
    resb STACK_SIZE
global kernel_stack_top                 ; C needs this for TSS.esp0
kernel_stack_top:

; --- Entry point -------------------------------------------------------------
section .text
global _start
extern kernel_main

_start:
    cli                                 ; GRUB leaves IF=0, but be explicit
    mov esp, kernel_stack_top
    push dword 0
    popfd                               ; EFLAGS = 0x2: clears DF, NT, IF, ...

    ; Keep ESP 16-byte aligned at the CALL: two 4-byte arguments + the 8-byte
    ; pad below make 16 bytes in total.
    sub esp, 8
    push ebx                            ; arg 2: const struct multiboot_info *
    push eax                            ; arg 1: uint32_t magic
    call kernel_main

    ; kernel_main() is noreturn; if it ever returns, stop cleanly.
.hang:
    cli
    hlt
    jmp .hang

; Tell the linker this object does not need an executable stack.
section .note.GNU-stack noalloc noexec nowrite progbits
