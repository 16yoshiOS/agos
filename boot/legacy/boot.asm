; =============================================================================
;  AGOS - Stage 1 Bootloader (boot.asm)
;  Target : x86 BIOS, 512-byte MBR, boots into 32-bit protected mode
; -----------------------------------------------------------------------------
;  Responsibilities, in order:
;    1. Normalise CPU state (segments, stack, direction flag).
;    2. Load the kernel from disk (INT 13h extensions / LBA) to 0x1_0000.
;    3. Switch the VGA to mode 13h (320x200, 256 colours) while BIOS is usable.
;    4. Enable and VERIFY the A20 line (BIOS -> port 0x92 -> keyboard ctrl).
;    5. Load a flat GDT, set CR0.PE, far-jump into 32-bit code.
;    6. Set up 32-bit segments + stack and call the kernel entry point.
;
;  Memory map used during boot (physical):
;    0x00000500 - 0x00007BFF   free low memory (A20 probe uses 0x500)
;    0x00007C00 - 0x00007DFF   this bootloader
;    0x00010000 - 0x0008FFFF   kernel image (up to 512 KiB)
;    0x00090000                initial 32-bit stack top (grows downward)
;
;  Build-time parameter:
;    KERNEL_SECTORS  number of 512-byte sectors of kernel.bin (set by Makefile
;                    via   nasm -DKERNEL_SECTORS=<n> ).
; =============================================================================

%ifndef KERNEL_SECTORS
    %error "KERNEL_SECTORS must be defined (nasm -DKERNEL_SECTORS=<n>)"
%endif

KERNEL_SEG      equ 0x1000              ; real-mode segment  -> phys 0x10000
KERNEL_BASE     equ 0x10000             ; linear load/entry address
STACK_TOP       equ 0x90000             ; 32-bit stack top
CODE_SEL        equ 0x08                ; GDT selector: ring-0 32-bit code
DATA_SEL        equ 0x10                ; GDT selector: ring-0 32-bit data
DISK_RETRIES    equ 3                   ; attempts per sector before giving up

BITS 16
ORG 0x7C00

; -----------------------------------------------------------------------------
; Entry point. BIOS jumps here with DL = boot drive number.
; Some BIOSes enter at 07C0:0000 instead of 0000:7C00, so we far-jump to
; normalise CS:IP before touching anything that depends on ORG.
; -----------------------------------------------------------------------------
start:
    jmp 0x0000:.init

.init:
    cli                                 ; no interrupts while changing SS:SP
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00                      ; stack grows down, below this code
    cld                                 ; string ops count upward
    sti                                 ; BIOS services below need interrupts

    mov [boot_drive], dl                ; remember the drive we booted from

    mov si, msg_boot
    call print16

; -----------------------------------------------------------------------------
; Step 2: load the kernel, one sector per call.
; One-sector reads avoid 64 KiB DMA-boundary and per-BIOS multi-sector limits;
; the destination segment advances by 0x20 paragraphs (= 512 bytes) each time.
; -----------------------------------------------------------------------------
load_kernel:
    mov word [sectors_left], KERNEL_SECTORS

.next_sector:
    mov byte [retries], DISK_RETRIES

.try:
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42                        ; INT 13h ext: read sectors (LBA)
    int 0x13
    jnc .ok

    ; Failure: reset the controller and retry a few times.
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    dec byte [retries]
    jnz .try
    mov si, msg_disk_err
    jmp fatal

.ok:
    add word [dap.segment], 0x20        ; next 512-byte slot
    add word [dap.lba], 1               ; next LBA (32-bit increment with carry)
    adc word [dap.lba + 2], 0
    dec word [sectors_left]
    jnz .next_sector

; -----------------------------------------------------------------------------
; Step 3: VGA mode 13h (320x200x256, linear framebuffer at 0xA0000).
; Must be done now: after the switch to protected mode the BIOS is unusable.
; -----------------------------------------------------------------------------
    mov ax, 0x0013
    int 0x10

; -----------------------------------------------------------------------------
; Step 4: A20 line.  Without A20, bit 20 of every address is forced to 0 and
; memory above 1 MiB aliases onto low memory - fatal for a 32-bit kernel.
; We test first (it may already be on), then try three methods, re-testing
; after each. If all fail we stop rather than run with corrupt addressing.
; -----------------------------------------------------------------------------
enable_a20:
    call a20_enabled
    jnz .done

    mov ax, 0x2401                      ; method 1: BIOS "enable A20"
    int 0x15
    call a20_enabled
    jnz .done

    in al, 0x92                         ; method 2: "fast A20" gate
    test al, 2
    jnz .kbc                            ;   already set but test failed -> skip
    or al, 2
    and al, 0xFE                        ;   bit 0 would reset the CPU - clear it
    out 0x92, al
    call a20_enabled
    jnz .done

.kbc:                                   ; method 3: 8042 keyboard controller
    call kbc_wait_write
    mov al, 0xAD                        ;   disable keyboard
    out 0x64, al
    call kbc_wait_write
    mov al, 0xD0                        ;   read output port
    out 0x64, al
    call kbc_wait_read
    in al, 0x60
    push ax
    call kbc_wait_write
    mov al, 0xD1                        ;   write output port
    out 0x64, al
    call kbc_wait_write
    pop ax
    or al, 2                            ;   bit 1 = A20 gate
    out 0x60, al
    call kbc_wait_write
    mov al, 0xAE                        ;   re-enable keyboard
    out 0x64, al
    call kbc_wait_write
    call a20_enabled
    jnz .done

    mov si, msg_a20_err
    jmp fatal

.done:

; -----------------------------------------------------------------------------
; Step 5: enter protected mode.
; -----------------------------------------------------------------------------
    cli                                 ; BIOS IVT is meaningless from here on
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1                           ; CR0.PE
    mov cr0, eax
    jmp CODE_SEL:pm_entry               ; far jump flushes the prefetch queue
                                        ; and loads CS with a 32-bit descriptor

; =============================================================================
;  16-bit helper routines
; =============================================================================

; print16: print the zero-terminated string at DS:SI via BIOS teletype.
print16:
    mov ah, 0x0E
    xor bx, bx
.loop:
    lodsb
    test al, al
    jz .end
    int 0x10
    jmp .loop
.end:
    ret

; fatal: print message at DS:SI and halt forever.
fatal:
    call print16
.halt:
    cli
    hlt
    jmp .halt

; a20_enabled: returns ZF=0 if A20 is ON, ZF=1 if OFF.
; Writes different bytes to 0000:0500 and FFFF:0510. With A20 off both map to
; physical 0x0500 (wrap-around), so the second write is visible through the
; first pointer. Original contents are restored. Clobbers AX only.
a20_enabled:
    pushf
    push ds
    push es
    push si
    push di
    cli
    xor ax, ax
    mov es, ax                          ; ES = 0x0000
    not ax
    mov ds, ax                          ; DS = 0xFFFF
    mov di, 0x0500
    mov si, 0x0510
    mov al, [es:di]                     ; AL = original byte at 0x0000:0500
    mov ah, [ds:si]                     ; AH = original byte at 0xFFFF:0510
    push ax                             ; keep both originals
    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF
    cmp byte [es:di], 0xFF              ; equal  => wrapped => A20 OFF
    setne dl                            ; DL = 1 if A20 ON (SETcc keeps flags)
    pop ax                              ; restore both original bytes
    mov [es:di], al
    mov [ds:si], ah
    mov al, dl                          ; carry result out in AL
    pop di
    pop si
    pop es
    pop ds
    popf                                ; restore caller's IF/DF (clobbers ZF)
    test al, al                         ; ZF=0 <=> A20 ON; this is our result
    ret

; kbc_wait_write: wait until the 8042 input buffer is empty (can accept a byte).
kbc_wait_write:
    in al, 0x64
    test al, 2
    jnz kbc_wait_write
    ret

; kbc_wait_read: wait until the 8042 output buffer has data.
kbc_wait_read:
    in al, 0x64
    test al, 1
    jz kbc_wait_read
    ret

; =============================================================================
;  32-bit protected-mode entry
; =============================================================================
BITS 32
pm_entry:
    mov ax, DATA_SEL                    ; flat 4 GiB data segment everywhere
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, STACK_TOP

    mov eax, KERNEL_BASE
    call eax                            ; kernel_main() must never return

.hang:                                  ; if it does, stop cleanly
    cli
    hlt
    jmp .hang

; =============================================================================
;  Data
; =============================================================================
BITS 16

; --- Disk Address Packet for INT 13h AH=42h (16 bytes) ----------------------
align 4
dap:
    db 0x10                             ; packet size
    db 0                                ; reserved
    dw 1                                ; sectors to transfer per call
    dw 0x0000                           ; destination offset
.segment:
    dw KERNEL_SEG                       ; destination segment (advanced in loop)
.lba:
    dd 1                                ; LBA of first kernel sector (0 = MBR)
    dd 0                                ; upper 32 bits of LBA

boot_drive:   db 0
retries:      db 0
sectors_left: dw 0

msg_boot:     db "AGOS: booting...", 13, 10, 0
msg_disk_err: db "Disk read error", 0
msg_a20_err:  db "A20 failed", 0

; --- Global Descriptor Table (flat model) -----------------------------------
align 8
gdt_start:
    dq 0x0000000000000000               ; 0x00 null descriptor (required)
    dq 0x00CF9A000000FFFF               ; 0x08 code: base 0, limit 4G, 32-bit,
                                        ;      ring 0, execute/read
    dq 0x00CF92000000FFFF               ; 0x10 data: base 0, limit 4G, 32-bit,
                                        ;      ring 0, read/write
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1          ; limit
    dd gdt_start                        ; linear base (ORG makes this absolute)

; --- MBR padding and boot signature -----------------------------------------
; If the code above grows past 510 bytes NASM fails with a negative TIMES count.
times 510 - ($ - $$) db 0
dw 0xAA55
