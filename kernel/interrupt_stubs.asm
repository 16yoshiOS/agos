; =============================================================================
;  AGOS - Interrupt entry stubs and descriptor-table loaders
;  (kernel/interrupt_stubs.asm)
; -----------------------------------------------------------------------------
;  * isr0..isr47      one tiny stub per vector (32 CPU exceptions + 16 IRQs)
;  * isr_common       saves CPU state, calls isr_dispatch(struct regs *)
;  * isr_stub_table   array of the 48 stub addresses (used by idt.c)
;  * gdt_flush / tss_flush / idt_flush   LGDT / LTR / LIDT wrappers
;
;  Stack frame seen by isr_dispatch (lowest address first). This layout MUST
;  match `struct regs` in isr.h:
;      gs fs es ds | edi esi ebp esp ebx edx ecx eax | int_no err_code |
;      eip cs eflags [useresp ss: only on a privilege change]
; =============================================================================

BITS 32

KERNEL_DATA_SEL equ 0x10
TSS_SEL         equ 0x28
NUM_VECTORS     equ 48

extern isr_dispatch

; --- Stub generators ---------------------------------------------------------
; Exceptions that push an error code: 8, 10-14, 17, 21, 29, 30. All other
; vectors push a dummy 0 so the frame always has the same shape.
%macro ISR_STUB 2                       ; %1 = vector, %2 = 1 if CPU pushes error code
    %assign vec %1
    global isr %+ vec
isr %+ vec:
    %if %2 == 0
        push dword 0                    ; dummy error code
    %endif
    push dword vec                      ; vector number
    jmp isr_common
%endmacro

section .text

%assign n 0
%rep NUM_VECTORS
    %if n == 8 || (n >= 10 && n <= 14) || n == 17 || n == 21 || n == 29 || n == 30
        ISR_STUB n, 1
    %else
        ISR_STUB n, 0
    %endif
    %assign n n + 1
%endrep

; --- Common path -------------------------------------------------------------
isr_common:
    pusha                               ; eax ecx edx ebx esp ebp esi edi
    push ds
    push es
    push fs
    push gs

    mov ax, KERNEL_DATA_SEL             ; interrupted code may have had other
    mov ds, ax                          ; segments loaded (future user mode)
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld                                 ; C ABI requires DF = 0

    push esp                            ; arg: struct regs *
    call isr_dispatch
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                          ; drop int_no + err_code
    iretd

; --- Table of stub addresses -------------------------------------------------
section .rodata
align 4
global isr_stub_table
isr_stub_table:
%assign n 0
%rep NUM_VECTORS
    dd isr %+ n
    %assign n n + 1
%endrep

; --- Descriptor-table loaders ------------------------------------------------
section .text

; void gdt_flush(const struct gdt_ptr *gp)
; Loads the GDT, reloads every segment register and far-jumps to reload CS.
global gdt_flush
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, KERNEL_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload_cs
.reload_cs:
    ret

; void tss_flush(void)
global tss_flush
tss_flush:
    mov ax, TSS_SEL
    ltr ax
    ret

; void idt_flush(const struct idt_ptr *ip)
global idt_flush
idt_flush:
    mov eax, [esp + 4]
    lidt [eax]
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
