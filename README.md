# AGOS

A from-scratch 32-bit x86 hobby operating system with a macOS-inspired GUI
(Light/Dark themes). Freestanding C + NASM, booted by GRUB (Multiboot 1).

**Status: Step 2** — GDT/TSS, IDT, PIC remap, PIT timer (100 Hz), PS/2
keyboard, serial log, panic screen, 32-bpp framebuffer with a themed welcome
screen. Roadmap: window manager, dock/menu bar, Settings, Task Manager,
Terminal, Browser.

## Layout

```
boot/start.asm            Multiboot header + entry stub (GRUB -> kernel_main)
boot/grub/grub.cfg        GRUB menu baked into the ISO
boot/legacy/boot.asm      Step 1 hand-written MBR bootloader (no longer built)
kernel/linker.ld          link at 1 MiB, ELF for GRUB
kernel/kernel.c           boot sequence + demo screen
kernel/gdt.c  idt.c  isr.c  interrupt_stubs.asm   descriptor tables, interrupt entry
kernel/pic.c  pit.c  keyboard.c  kbd_decode.c     IRQ controller, timer, PS/2
kernel/gfx.c  font.c  vgatext.c                   framebuffer, font, text fallback
kernel/serial.c  panic.c  lib.c  io.h             debug log, panic, libc subset
tests/                    host unit tests + hardware simulation
tools/check_kernel.py     Multiboot/ELF validator (runs in `make`)
tools/modal_build.py      build everything on Modal
```

## Build

```
make            # build/kernel.elf (+ Multiboot validation)
make test       # host tests: logic + simulated PIC/PIT/PS2 hardware
make iso        # build/agos.iso  (needs grub-pc-bin grub-common xorriso mtools)
make run        # boot the ISO in QEMU (serial log on this terminal)
make run-kernel # QEMU's own Multiboot loader, no GRUB (VGA text fallback)
```

Debian/Ubuntu packages: `nasm gcc binutils make python3 grub-pc-bin grub-common
xorriso mtools` (+ `qemu-system-x86` to run).

### On Modal

```
pip install modal && modal setup
modal run tools/modal_build.py        # -> dist/agos.iso, dist/kernel.elf
```

### With Docker

```
docker run --rm -v "$PWD":/src -w /src debian:bookworm bash -c \
  "apt-get update && apt-get install -y nasm gcc binutils make python3 \
   grub-pc-bin grub-common xorriso mtools && make iso"
```

## Running

`qemu-system-i386 -cdrom build/agos.iso -m 128 -serial stdio`, or write the ISO
to a USB stick / attach it to VirtualBox, VMware or Hyper-V Gen1 (BIOS). The
ISO is BIOS-bootable; UEFI-only machines are not supported yet.

In the demo screen: type to echo text, **Backspace** edits, **Tab** switches
Light/Dark. The serial console shows the boot log and one line per second.

## Boot flow

1. BIOS -> GRUB (El Torito) -> sets 1024x768x32 via VBE, loads `kernel.elf` at 1 MiB.
2. `_start` (EAX = Multiboot magic, EBX = info pointer) sets a stack, calls `kernel_main`.
3. `kernel_main`: serial -> GDT/TSS -> IDT -> PIC remap (IRQ 0-15 -> vectors 32-47)
   -> PIT 100 Hz -> keyboard -> `sti` -> framebuffer from GRUB -> render loop (`hlt`).
4. CPU exceptions without a handler end in a panic screen with a register dump.

## Notes

* No paging yet: physical addresses are used directly (the framebuffer too).
* The back buffer is 1920x1080x4 bytes in `.bss` (8 MiB, no cost in the ISO);
  QEMU needs `-m 16` or more.
* Interrupt stubs keep 4-byte stack alignment (`-mpreferred-stack-boundary=2`)
  and the kernel is built with `-mgeneral-regs-only`: no SSE/x87 until the FPU
  state is managed.
